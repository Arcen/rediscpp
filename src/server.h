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
	class blocked_exception : public std::exception
	{
		std::string what_;
	public:
		blocked_exception() : std::exception(){}
		blocked_exception(const blocked_exception & rhs){}
		blocked_exception(const char * const & msg) : what_(msg){}
		virtual ~blocked_exception() throw() {}
		virtual const char* what() const throw() { return what_.c_str(); }
	};
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
		string_type(std::string && string_value_, const timeval_type & current);
		virtual ~string_type(){}
		virtual std::string get_type() { return std::string("string"); }
		const std::string & get();
		std::string & ref() { return string_value; }
		void set(const std::string & str) { string_value = str; }
		int64_t append(const std::string & str)
		{
			string_value += str;
			return string_value.size();
		}
		int64_t setrange(size_t offset, const std::string & str)
		{
			size_t new_size = offset + str.size();
			if (string_value.size() < new_size) {
				string_value.resize(new_size);
			}
			std::copy(str.begin(), str.end(), string_value.begin() + offset);
			return string_value.size();
		}
	};
	class list_type : public value_interface
	{
		std::list<std::string> value;
		size_t size_;
	public:
		list_type(const timeval_type & current)
			: value_interface(current)
			, size_(0)
		{
		}
		virtual ~list_type(){}
		virtual std::string get_type() { return std::string("list"); }
		const std::list<std::string> & get();
		void lpush(const std::vector<std::string*> & elements)
		{
			for (auto it = elements.begin(), end = elements.end(); it != end; ++it) {
				value.insert(value.begin(), **it);
			}
			size_ += elements.size();
		}
		void rpush(const std::vector<std::string*> & elements)
		{
			for (auto it = elements.begin(), end = elements.end(); it != end; ++it) {
				value.insert(value.end(), **it);
			}
			size_ += elements.size();
		}
		bool linsert(const std::string & pivot, const std::string & element, bool before)
		{
			auto it = std::find(value.begin(), value.end(), pivot);
			if (it == value.end()) {
				return false;
			}
			if (!before) {
				++it;
			}
			value.insert(it, element);
			++size_;
			return true;
		}
		void lpush(const std::string & element)
		{
			value.push_front(element);
			++size_;
		}
		void rpush(const std::string & element)
		{
			value.push_back(element);
			++size_;
		}
		std::string lpop()
		{
			if (size_ == 0) {
				throw std::runtime_error("lpop failed. list is empty");
			}
			std::string result = value.front();
			value.pop_front();
			--size_;
			return result;
		}
		std::string rpop()
		{
			if (size_ == 0) {
				throw std::runtime_error("rpop failed. list is empty");
			}
			std::string result = value.back();
			value.pop_back();
			--size_;
			return result;
		}
		size_t size() const { return size_; }
		bool empty() const { return size_ == 0; }
		std::list<std::string>::const_iterator get_it(size_t index) const
		{
			if (size_ <= index) {
				return value.end();
			}
			if (index <= size_ / 2) {
				std::list<std::string>::const_iterator it = value.begin();
				for (auto i = 0; i < index; ++i) {
					++it;
				}
				return it;
			}
			std::list<std::string>::const_iterator it = value.end();
			for (auto i = size_; index < i; --i) {
				--it;
			}
			return it;
		}
	private:
		std::list<std::string>::iterator get_it_internal(size_t index)
		{
			if (size_ <= index) {
				return value.end();
			}
			if (index <= size_ / 2) {
				std::list<std::string>::iterator it = value.begin();
				for (auto i = 0; i < index; ++i) {
					++it;
				}
				return it;
			}
			std::list<std::string>::iterator it = value.end();
			for (auto i = size_; index < i; --i) {
				--it;
			}
			return it;
		}
	public:
		bool set(int64_t index, const std::string & newval)
		{
			if (index < 0 || size_ <= index) return false;
			auto it = get_it_internal(index);
			*it = newval;
			return true;
		}
		std::pair<std::list<std::string>::const_iterator,std::list<std::string>::const_iterator> get_range(size_t start, size_t end) const
		{
			start = std::min(size_, start);
			end = std::min(size_, end);
			if (end <= start) {
				return std::make_pair(value.end(), value.end());
			}
			std::list<std::string>::const_iterator sit = get_it(start);
			if (size_ <= end) {
				return std::make_pair(sit, value.end());
			}
			if (start == end) {
				return std::make_pair(sit, sit);
			}
			std::list<std::string>::const_iterator eit;
			if (end - start < size_ - end) {
				eit = sit;
				for (size_t i = start; i < end; ++i) {
					++eit;
				}
			} else {
				eit = get_it(end);
			}
			return std::make_pair(sit, eit);
		}
	private:
		std::pair<std::list<std::string>::iterator,std::list<std::string>::iterator> get_range_internal(size_t start, size_t end)
		{
			start = std::min(size_, start);
			end = std::min(size_, end);
			if (end <= start) {
				return std::make_pair(value.end(), value.end());
			}
			std::list<std::string>::iterator sit = get_it_internal(start);
			if (size_ <= end) {
				return std::make_pair(sit, value.end());
			}
			if (start == end) {
				return std::make_pair(sit, sit);
			}
			std::list<std::string>::iterator eit;
			if (end - start < size_ - end) {
				eit = sit;
				for (size_t i = start; i < end; ++i) {
					++eit;
				}
			} else {
				eit = get_it_internal(end);
			}
			return std::make_pair(sit, eit);
		}
	public:
		///@param[in] count 0ならすべてを消す、正ならfrontから指定数を消す、負ならbackから指定数を消す
		///@return 削除数
		size_t lrem(int64_t count, const std::string & target)
		{
			size_t removed = 0;
			if (count == 0) {
				for (auto it = value.begin(); it != value.end();) {
					if (*it == target) {
						it = value.erase(it);
						++removed;
					} else {
						++it;
					}
				}
			} else if (0 < count) {
				for (auto it = value.begin(); it != value.end();) {
					if (*it == target) {
						it = value.erase(it);
						++removed;
						if (count == removed) {
							break;
						}
					} else {
						++it;
					}
				}
			} else {
				count = - count;
				if (!value.empty()) {
					auto it = value.end();
					--it;
					while (true) {
						if (*it == target) {
							it = value.erase(it);
							++removed;
							if (count == removed) {
								break;
							}
						}
						if (it == value.begin()) {
							break;
						}
						--it;
					}
				}
			}
			size_ -= removed;
			return removed;
		}
		///[start,end)の範囲になるように前後を削除する
		void trim(size_t start, size_t end)
		{
			start = std::min(size_, start);
			end = std::min(size_, end);
			if (end <= start) {
				value.clear();
				size_ = 0;
				return;
			}
			auto range = get_range_internal(start, end);
			size_ = end - start;
			value.erase(value.begin(), range.first);
			value.erase(range.second, value.end());
		}
	};
	class hash_type : public value_interface
	{
		std::unordered_map<std::string, std::string> value;
	public:
		hash_type(const timeval_type & current)
			: value_interface(current)
		{
		}
		virtual ~hash_type(){}
		virtual std::string get_type() { return std::string("hash"); }
		size_t hdel(const std::vector<std::string*> & fields)
		{
			size_t removed = 0;
			for (auto it = fields.begin(), end = fields.end(); it != end; ++it) {
				auto & field = **it;
				auto vit = value.find(field);
				if (vit != value.end()) {
					value.erase(vit);
					++removed;
				}
			}
			return removed;
		}
		bool hexists(const std::string field) const { return value.find(field) != value.end(); }
		bool empty() const { return value.empty(); }
		std::pair<std::string,bool> hget(const std::string field) const
		{
			auto it = value.find(field);
			if (it != value.end()) {
				return std::make_pair(it->second, true);
			}
			return std::make_pair(std::string(), false);
		}
		std::pair<std::unordered_map<std::string, std::string>::const_iterator,std::unordered_map<std::string, std::string>::const_iterator> hgetall() const
		{
			return std::make_pair(value.begin(), value.end());
		}
		size_t size() const { return value.size(); }
		bool hset(const std::string & field, const std::string & val, bool nx = false)
		{
			auto it = value.find(field);
			if (it != value.end()) {
				if (nx) {
					return false;
				}
				it->second = val;
				return false;
			} else {
				value[field] = val;
				return true;//created
			}
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
		template<typename T>
		std::shared_ptr<T> get_as(const std::string & key, const timeval_type & current) const
		{
			std::shared_ptr<value_interface> val = get(key, current);
			if (!val) {
				return std::shared_ptr<T>();
			}
			std::shared_ptr<T> value = std::dynamic_pointer_cast<T>(val);
			if (!value) {
				throw std::runtime_error("ERR type mismatch");
			}
			return value;
		}
		std::shared_ptr<string_type> get_string(const std::string & key, const timeval_type & current) const { return get_as<string_type>(key, current); }
		std::shared_ptr<list_type> get_list(const std::string & key, const timeval_type & current) const { return get_as<list_type>(key, current); }
		std::shared_ptr<hash_type> get_hash(const std::string & key, const timeval_type & current) const { return get_as<hash_type>(key, current); }
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
		bool blocked;//for list
		timeval_type blocked_till;
		std::weak_ptr<client_type> self;
	public:
		client_type(server_type & server_, std::shared_ptr<socket_type> & client_, const std::string & password_);
		bool parse();
		const arguments_type & get_arguments() const { return arguments; }
		const std::string & get_argument(int index) const { return arguments[index]; }
		const std::vector<std::string*> & get_keys() const { return keys; }
		const std::vector<std::string*> & get_values() const { return values; }
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
		void response_start_multi_bulk(size_t count);
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
		bool is_blocked() const { return blocked; }
		timeval_type get_blocked_till() const { return blocked_till; }
		void start_blocked(int64_t sec)
		{
			blocked = true;
			if (0 < sec) {
				blocked_till = current_time;
				blocked_till.add_msec(sec * 1000);
			} else {
				blocked_till.epoc();
			}
		}
		void end_blocked() { blocked = false; }
		bool still_block() const
		{
			return blocked && (blocked_till.is_epoc() || current_time < blocked_till);
		}
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
			list_pushed_type,///<listに値が何か追加された場合
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
		std::shared_ptr<timer_type> timer;
		std::map<socket_type*,std::shared_ptr<client_type>> clients;
		mutex_type blocked_mutex;
		std::set<std::shared_ptr<client_type>> blocked_clients;
		std::shared_ptr<socket_type> listening;
		std::map<std::string,std::string> store;
		std::string password;
		std::vector<std::shared_ptr<database_type>> databases;
		std::vector<std::shared_ptr<worker_type>> thread_pool;
		sync_queue<std::shared_ptr<job_type>> jobs;
		bool shutdown;
		std::vector<uint8_t> bits_table;
		static void client_callback(pollable_type * p, int events);
		static void server_callback(pollable_type * p, int events);
		static void event_callback(pollable_type * p, int events);
		static void timer_callback(pollable_type * p, int events);
		void on_server(socket_type * s, int events);
		void on_client(socket_type * s, int events);
		void on_event(event_type * e, int events);
		void on_timer(timer_type * e, int events);
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
		void remove_client(std::shared_ptr<client_type> client, bool now = false);
		void append_client(std::shared_ptr<client_type> client, bool now = false);
		std::map<std::string,api_info> api_map;
		void build_api_map();
		void blocked(std::shared_ptr<client_type> client)
		{
			if (!client) return;
			mutex_locker locker(blocked_mutex);
			blocked_clients.insert(client);
		}
		void unblocked(std::shared_ptr<client_type> client)
		{
			if (!client) return;
			mutex_locker locker(blocked_mutex);
			blocked_clients.insert(client);
		}
		void excecute_blocked_client(bool now = false);
		void notify_list_pushed();
		int64_t pos_fix(int64_t pos, int64_t size)
		{
			if (pos < 0) {
				pos = - pos;
				if (size < pos) {
					return 0;
				} else {
					return size - pos;
				}
			} else {
				if (size < pos) {
					return size;
				}
				return pos;
			}
		}
		int64_t incrby(const std::string & value, int64_t count);
		std::string incrbyfloat(const std::string & value, const std::string & count);

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
		bool api_set_internal(client_type * client, bool nx, bool xx, int64_t expire);
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
		bool api_incrdecr_internal(client_type * client, int64_t count);
		bool api_incrbyfloat(client_type * client);
		bool api_bitcount(client_type * client);
		bool api_bitop(client_type * client);
		bool api_getbit(client_type * client);
		bool api_setbit(client_type * client);
		//lists api
		bool api_blpop(client_type * client);
		bool api_brpop(client_type * client);
		bool api_brpoplpush(client_type * client);
		bool api_lindex(client_type * client);
		bool api_linsert(client_type * client);
		bool api_llen(client_type * client);
		bool api_lpop(client_type * client);
		bool api_lpush(client_type * client);
		bool api_lpushx(client_type * client);
		bool api_lrange(client_type * client);
		bool api_lrem(client_type * client);
		bool api_lset(client_type * client);
		bool api_ltrim(client_type * client);
		bool api_rpop(client_type * client);
		bool api_rpoplpush(client_type * client);
		bool api_rpush(client_type * client);
		bool api_rpushx(client_type * client);
		bool api_lpush_internal(client_type * client, bool left, bool exist);
		bool api_lpop_internal(client_type * client, bool left);
		bool api_bpop_internal(client_type * client, bool left, bool rpoplpush);
		//hashes api
		bool api_hdel(client_type * client);
		bool api_hexists(client_type * client);
		bool api_hget(client_type * client);
		bool api_hgetall(client_type * client);
		bool api_hincrby(client_type * client);
		bool api_hincrbyfloat(client_type * client);
		bool api_hkeys(client_type * client);
		bool api_hlen(client_type * client);
		bool api_hmget(client_type * client);
		bool api_hmset(client_type * client);
		bool api_hset(client_type * client);
		bool api_hsetnx(client_type * client);
		bool api_hvals(client_type * client);
		bool api_hgetall_internal(client_type * client, bool keys, bool vals);
		bool api_hset_internal(client_type * client, bool nx);
		//sets api
		bool api_sadd(client_type * client);
		bool api_scard(client_type * client);
		bool api_sdiff(client_type * client);
		bool api_sdiffstore(client_type * client);
		bool api_sinter(client_type * client);
		bool api_sinterstore(client_type * client);
		bool api_sismember(client_type * client);
		bool api_smembers(client_type * client);
		bool api_smove(client_type * client);
		bool api_spop(client_type * client);
		bool api_srandmember(client_type * client);
		bool api_srem(client_type * client);
		bool api_sunion(client_type * client);
		bool api_sunionstore(client_type * client);
		//sorted sets api
		bool api_zadd(client_type * client);
		bool api_zcard(client_type * client);
		bool api_zcount(client_type * client);
		bool api_zincrby(client_type * client);
		bool api_zinterstore(client_type * client);
		bool api_zrange(client_type * client);
		bool api_zrangebystore(client_type * client);
		bool api_zrank(client_type * client);
		bool api_zrem(client_type * client);
		bool api_zremrangebyrank(client_type * client);
		bool api_zremrangebyscore(client_type * client);
		bool api_zrevrange(client_type * client);
		bool api_zrevrangebyscore(client_type * client);
		bool api_zrevrank(client_type * client);
		bool api_zscore(client_type * client);
		bool api_zunionstore(client_type * client);
	};
}

#endif
