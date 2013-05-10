#ifndef INCLUDE_REDIS_CPP_SERVER_H
#define INCLUDE_REDIS_CPP_SERVER_H

#include "network.h"
#include "timeval.h"
#include "thread.h"
#include "type_interface.h"
#include "database.h"

namespace rediscpp
{
	class server_type;
	class client_type;
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
		database_write_locker writable_db(int index, client_type * client, bool rdlock = false);
		database_read_locker readable_db(int index, client_type * client);
		database_write_locker writable_db(client_type * client, bool rdlock = false);
		database_read_locker readable_db(client_type * client);
	private:
		void remove_client(std::shared_ptr<client_type> client, bool now = false);
		void append_client(std::shared_ptr<client_type> client, bool now = false);
		std::map<std::string,api_info> api_map;
		void build_api_map();
		void blocked(std::shared_ptr<client_type> client);
		void unblocked(std::shared_ptr<client_type> client);
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
		bool save(const std::string & path);

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
		bool api_sort(client_type * client);
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
		bool api_soperaion_internal(client_type * client, int type, bool store);
		//sorted sets api
		bool api_zadd(client_type * client);
		bool api_zcard(client_type * client);
		bool api_zcount(client_type * client);
		bool api_zincrby(client_type * client);
		bool api_zinterstore(client_type * client);
		bool api_zrange(client_type * client);
		bool api_zrangebyscore(client_type * client);
		bool api_zrank(client_type * client);
		bool api_zrem(client_type * client);
		bool api_zremrangebyrank(client_type * client);
		bool api_zremrangebyscore(client_type * client);
		bool api_zrevrange(client_type * client);
		bool api_zrevrangebyscore(client_type * client);
		bool api_zrevrank(client_type * client);
		bool api_zscore(client_type * client);
		bool api_zunionstore(client_type * client);
		bool api_zoperaion_internal(client_type * client, int type);
		bool api_zrange_internal(client_type * client, bool rev);
		bool api_zrangebyscore_internal(client_type * client, bool rev);
		bool api_zrank_internal(client_type * client, bool rev);
	};
}

#endif
