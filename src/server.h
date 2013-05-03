#ifndef INCLUDE_REDIS_CPP_SERVER_H
#define INCLUDE_REDIS_CPP_SERVER_H

#include "network.h"
#include "timeval.h"
#include "thread.h"

namespace rediscpp
{
	class server_type;
	class client_type;
	typedef std::vector<std::string> arguments_type;
	class value_interface
	{
	protected:
		timeval_type last_modified_time;///<最後に修正した時間(WATCH用)
		timeval_type expire_time;///<0,0なら有効期限無し、消失する日時を保存する
	public:
		value_interface(const timeval_type & current)
			: last_modified_time(current)
			, expire_time(0,0)
		{
		}
		virtual ~value_interface(){}
		virtual std::string get_type() = 0;
		bool is_expired(const timeval_type & current)
		{
			return ! expire_time.is_epoc() && expire_time <= current;
		}
		void expire(const timeval_type & at)
		{
			expire_time = at;
		}
		void persist()
		{
			expire_time.epoc();
		}
		bool is_expiring() const { return ! expire_time.is_epoc(); }
		timeval_type get_last_modified_time() const { return last_modified_time; }
		timeval_type ttl(const timeval_type & current) const
		{
			if (is_expiring()) {
				if (current < expire_time) {
					return expire_time - current;
				}
			}
			return timeval_type(0,0);
		}
		void update(const timeval_type & current) { last_modified_time = current; }
	};
	class string_type : public value_interface
	{
		std::string string_value;
	public:
		string_type(const std::string & string_value_, const timeval_type & current);
		virtual ~string_type(){}
		virtual std::string get_type() { return std::string("string"); }
		const std::string & get();
		int64_t append(const std::string & str)
		{
			string_value += str;
			return string_value.size();
		}
		int64_t setrange(int64_t offset, const std::string & str)
		{
			if (string_value.size() < offset) {
				string_value.resize(offset);
				string_value.append(str);
			} else if (string_value.size() < offset + str.size()) {
				string_value.resize(offset + str.size());
				std::copy(str.begin(), str.end(), string_value.begin() + offset);
			} else {
				std::copy(str.begin(), str.end(), string_value.begin() + offset);
			}
			return string_value.size();
		}
	};
	class database_type;
	class database_write_locker
	{
		database_type * database;
		std::shared_ptr<rwlock_locker> locker;
	public:
		database_write_locker(database_type * database_, client_type * client);
		database_type * get() { return database; }
		database_type * operator->() { return database; }
	};
	class database_read_locker
	{
		database_type * database;
		std::shared_ptr<rwlock_locker> locker;
	public:
		database_read_locker(database_type * database_, client_type * client);
		const database_type * get() { return database; }
		const database_type * operator->() { return database; }
	};
	class database_type
	{
		friend class database_write_locker;
		friend class database_read_locker;
		std::unordered_map<std::string,std::shared_ptr<value_interface>> values;
		mutable mutex_type expire_mutex;
		mutable std::multimap<timeval_type,std::string> expires;
		rwlock_type rwlock;
		database_type(const database_type &);
	public:
		database_type(){};
		size_t get_dbsize() const { return values.size(); }
		void clear() { values.clear(); }
		std::shared_ptr<value_interface> get(const std::string & key, const timeval_type & current) const
		{
			auto it = values.find(key);
			if (it == values.end()) {
				return std::shared_ptr<value_interface>();
			}
			auto value = it->second;
			if (value->is_expired(current)) {
				//values.erase(it);
				return std::shared_ptr<value_interface>();
			}
			return value;
		}
		bool erase(const std::string & key, const timeval_type & current)
		{
			auto it = values.find(key);
			if (it == values.end()) {
				return false;
			}
			auto value = it->second;
			if (value->is_expired(current)) {
				values.erase(it);
				return false;
			}
			values.erase(it);
			return true;
		}
		bool insert(const std::string & key, std::shared_ptr<value_interface> value, const timeval_type & current)
		{
			auto it = values.find(key);
			if (it == values.end()) {
				return values.insert(std::make_pair(key, value)).second;
			} else {
				if (it->second->is_expired(current)) {
					it->second = value;
					return true;
				}
				return false;
			}
		}
		void replace(const std::string & key, std::shared_ptr<value_interface> value)
		{
			values[key] = value;
		}
		std::string randomkey(const timeval_type & current)
		{
			while (!values.empty()) {
				auto it = values.begin();
				std::advance(it, rand() % values.size());
				if (it->second->is_expired(current)) {
					values.erase(it);
					continue;
				}
				return it->first;
			}
			return std::string();
		}
		void regist_expiring_key(timeval_type tv, const std::string & key) const
		{
			mutex_locker locker(expire_mutex);
			expires.insert(std::make_pair(tv, key));
		}
		void flush_expiring_key(const timeval_type & current)
		{
			mutex_locker locker(expire_mutex);
			if (expires.empty()) {
				return;
			}
			auto it = expires.begin(), end = expires.end();
			for (; it != end && it->first < current; ++it) {
				auto vit = values.find(it->second);
				if (vit != values.end() && vit->second->is_expired(current)) {
					values.erase(vit);
				}
			}
			expires.erase(expires.begin(), it);
		}
		void match(std::unordered_set<std::string> & result, const std::string & pattern) const
		{
			if (pattern == "*") {
				for (auto it = values.begin(), end = values.end(); it != end; ++it) {
					auto & key = it->first;
					result.insert(key);
				}
			} else {
				for (auto it = values.begin(), end = values.end(); it != end; ++it) {
					auto & key = it->first;
					if (pattern_match(pattern, key)) {
						result.insert(key);
					}
				}
			}
		}
	};
	typedef bool (server_type::*api_function_type)(client_type * client);
	struct api_info
	{
		api_function_type function;
		size_t min_argc;
		size_t max_argc;
		//c : command, s : string, k : key, v : value, d : db index, t : time, i : integer, f : float
		std::string arg_types;
		api_info()
			: function(NULL)
			, min_argc(1)
			, max_argc(1)
			, arg_types("c")
		{
		}
		api_info & set(api_function_type function_)
		{
			function = function_;
			return *this;
		}
		api_info & argc(size_t argc = 1)
		{
			min_argc = max_argc = argc;
			return *this;
		}
		api_info & argc(size_t min_argc_, int max_argc_)
		{
			min_argc = min_argc_;
			max_argc = max_argc_;
			return *this;
		}
		api_info & argc_gte(size_t argc = 1)
		{
			min_argc = argc;
			max_argc = std::numeric_limits<size_t>::max();
			return *this;
		}
		api_info & type(const std::string & arg_types_)
		{
			arg_types = arg_types_;
			return *this;
		}
	};
	class client_type
	{
		friend class server_type;
		server_type & server;
		std::shared_ptr<socket_type> client;
		arguments_type arguments;
		std::vector<std::string*> keys;
		std::vector<std::string*> values;
		int argument_count;
		int argument_index;
		int argument_size;
		std::string password;
		static const int argument_is_null = -1;
		static const int argument_is_undefined = -2;
		int db_index;
		bool transaction;
		bool multi_executing;
		std::list<arguments_type> transaction_arguments;
		std::set<std::tuple<std::string,int,timeval_type>> watching;
		std::vector<uint8_t> write_cache;
		timeval_type current_time;
		int events;//for thread
		std::weak_ptr<client_type> self;
	public:
		client_type(server_type & server_, std::shared_ptr<socket_type> & client_, const std::string & password_);
		bool parse();
		arguments_type & get_arguments() { return arguments; }
		std::string & get_argument(int index) { return arguments[index]; }
		void response_status(const std::string & state);
		void response_error(const std::string & state);
		void response_ok();
		void response_pong();
		void response_queued();
		void response_integer(int64_t value);
		void response_integer0();
		void response_integer1();
		void response_bulk(const std::string & bulk, bool not_null = true);
		void response_null();
		void response_null_multi_bulk();
		void response_start_multi_bulk(int count);
		void response_raw(const std::string & raw);
		void flush();
		void close_after_send() { client->close_after_send(); }
		bool require_auth(const std::string & auth);
		bool auth(const std::string & password_);
		void select(int index) { db_index = index; }
		int get_db_index() { return db_index; }
		bool multi();
		bool exec();
		void discard();
		bool in_exec() const;
		bool queuing(const std::string & command);
		void unwatch() { watching.clear(); }
		void watch(const std::string & key);
		std::set<std::tuple<std::string,int,timeval_type>> & get_watching() { return watching; }
		size_t get_transaction_size() { return transaction_arguments.size(); }
		bool unqueue();
		timeval_type get_time() const { return current_time; }
		void process();
		void set(std::shared_ptr<client_type> self_) { self = self_; }
		std::shared_ptr<client_type> get() { return self.lock(); }
	private:
		void inline_command_parser(const std::string & line);
		bool parse_line(std::string & line);
		bool parse_data(std::string & data, int size);
		bool execute();
		bool execute(const api_info & info);
	};
	class worker_type : public thread_type
	{
		server_type & server;
	public:
		worker_type(server_type & server_);
		virtual void run();
	};
	class job_type
	{
	public:
		enum job_types
		{
			add_type,
			del_type,
		};
		job_types type;
		std::shared_ptr<client_type> client;
		job_type(job_types type_, std::shared_ptr<client_type> client_)
			: type(type_)
			, client(client_)
		{
		}
	};
	class server_type
	{
		friend class client_type;
		std::shared_ptr<poll_type> poll;
		std::shared_ptr<event_type> event;
		std::map<socket_type*,std::shared_ptr<client_type>> clients;
		std::shared_ptr<socket_type> listening;
		std::map<std::string,std::string> store;
		std::string password;
		std::vector<std::shared_ptr<database_type>> databases;
		std::vector<std::shared_ptr<worker_type>> thread_pool;
		sync_queue<std::shared_ptr<job_type>> jobs;
		bool shutdown;
		static void client_callback(pollable_type * p, int events);
		static void server_callback(pollable_type * p, int events);
		static void event_callback(pollable_type * p, int events);
		void on_server(socket_type * s, int events);
		void on_client(socket_type * s, int events);
		void on_event(event_type * e, int events);
	public:
		server_type();
		~server_type();
		void startup_threads(int threads);
		void shutdown_threads();
		bool start(const std::string & hostname, const std::string & port, int threads);
		void process();
		database_write_locker writable_db(int index, client_type * client) { return database_write_locker(databases.at(index).get(), client); }
		database_read_locker readable_db(int index, client_type * client) { return database_read_locker(databases.at(index).get(), client); }
		database_write_locker writable_db(client_type * client) { return writable_db(client->get_db_index(), client); }
		database_read_locker readable_db(client_type * client) { return readable_db(client->get_db_index(), client); }
	private:
		void remove_client(std::shared_ptr<client_type> client);
		std::map<std::string,api_info> api_map;
		void build_api_map();
		//connection api
		bool api_auth(client_type * client);
		bool api_ping(client_type * client);
		bool api_quit(client_type * client);
		bool api_echo(client_type * client);
		bool api_select(client_type * client);
		//server api
		bool api_dbsize(client_type * client);
		bool api_flushall(client_type * client);
		bool api_flushdb(client_type * client);
		bool api_shutdown(client_type * client);
		bool api_time(client_type * client);
		//transactions api
		bool api_multi(client_type * client);
		bool api_exec(client_type * client);
		bool api_discard(client_type * client);
		bool api_watch(client_type * client);
		bool api_unwatch(client_type * client);
		//keys api
		bool api_keys(client_type * client);
		bool api_del(client_type * client);
		bool api_exists(client_type * client);
		bool api_expire(client_type * client);
		bool api_expireat(client_type * client);
		bool api_pexpire(client_type * client);
		bool api_pexpireat(client_type * client);
		bool api_expire_internal(client_type * client, bool sec, bool ts);
		bool api_persist(client_type * client);
		bool api_ttl(client_type * client);
		bool api_pttl(client_type * client);
		bool api_ttl_internal(client_type * client, bool sec);
		bool api_move(client_type * client);
		bool api_randomkey(client_type * client);
		bool api_rename(client_type * client);
		bool api_renamenx(client_type * client);
		bool api_type(client_type * client);
		//strings api
		bool api_get(client_type * client);
		bool api_set_internal(client_type * client, const std::string & key, const std::string & value, bool nx, bool xx, int64_t expire);
		bool api_set(client_type * client);
		bool api_setnx(client_type * client);
		bool api_setex(client_type * client);
		bool api_psetex(client_type * client);
		bool api_strlen(client_type * client);
		bool api_append(client_type * client);
		bool api_getrange(client_type * client);
		bool api_setrange(client_type * client);
		bool api_getset(client_type * client);
		bool api_mget(client_type * client);
		bool api_mset(client_type * client);
		bool api_msetnx(client_type * client);
		bool api_decr(client_type * client);
		bool api_decrby(client_type * client);
		bool api_incr(client_type * client);
		bool api_incrby(client_type * client);
		bool api_incrbyfloat(client_type * client);
		bool api_bitcount(client_type * client);
		bool api_bitop(client_type * client);
		bool api_getbit(client_type * client);
		bool api_setbit(client_type * client);
	};
}

#endif
