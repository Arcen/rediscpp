#ifndef INCLUDE_REDIS_CPP_SERVER_H
#define INCLUDE_REDIS_CPP_SERVER_H

#include "network.h"
#include <list>

namespace rediscpp
{
	class server_type;
	class timeval_type : public timeval
	{
	public:
		bool operator==(const timeval_type & rhs) const { return (tv_sec == rhs.tv_sec) && (tv_usec == rhs.tv_usec); }
		bool operator!=(const timeval_type & rhs) const { return !(*this == rhs); }
		bool operator<(const timeval_type & rhs) const { return (tv_sec != rhs.tv_sec) ? tv_sec < rhs.tv_sec : tv_usec < rhs.tv_usec; }
		bool operator>=(const timeval_type & rhs) const { return !(*this < rhs); }
		bool operator>(const timeval_type & rhs) const { return (rhs < *this); }
		bool operator<=(const timeval_type & rhs) const { return !(rhs < *this); }
		timeval_type();
		timeval_type(const timeval_type & rhs) { tv_sec = rhs.tv_sec; tv_usec = rhs.tv_usec; }
		timeval_type & operator=(const timeval_type & rhs) { tv_sec = rhs.tv_sec; tv_usec = rhs.tv_usec; return *this; }
		void update();
	};
	class value_interface
	{
	public:
		virtual ~value_interface(){}
		timeval_type last_modified_time;
	};
	class string_type : public value_interface
	{
	public:
		virtual ~string_type(){}
	};
	class database_type
	{
	public:
		std::map<std::string,std::shared_ptr<value_interface>> values;
	};
	class client_type
	{
		std::shared_ptr<socket_type> client;
		typedef std::pair<std::string,bool> argument_type;
		typedef std::vector<argument_type> arguments_type;
		arguments_type arguments;
		int argument_count;
		int argument_index;
		int argument_size;
		std::string password;
		static const int argument_is_null = -1;
		static const int argument_is_undefined = -2;
		int db_index;
		bool transaction;
		std::list<arguments_type> transaction_arguments;
		std::set<std::tuple<std::string,int,timeval_type>> watching;
	public:
		client_type(std::shared_ptr<socket_type> client_, const std::string & password_)
			: client(client_)
			, argument_count(0)
			, argument_index(0)
			, argument_size(argument_is_undefined)
			, password(password_)
			, db_index(0)
			, transaction(false)
		{
		}
		bool parse(server_type * server);
		arguments_type & get_arguments() { return arguments; }
		void response_status(const std::string & state);
		void response_error(const std::string & state);
		void response_ok();
		void response_pong();
		void response_queued();
		void response_integer(int64_t value);
		void response_integer0();
		void response_integer1();
		void response_bulk(const std::string & bulk, bool not_null = true);
		void response_null_multi_bulk();
		void response_raw(const std::string & raw);
		void response_start_multi_bulk(int count);
		void close_after_send() { client->close_after_send(); }
		bool require_auth(const std::string & auth);
		bool auth(const std::string & password_);
		void select(int index) { db_index = index; }
		int get_db_index() { return db_index; }
		bool multi();
		bool exec();
		void discard();
		bool in_transaction() { return transaction; }
		bool queuing(const std::string & command);
		void unwatch() { watching.clear(); }
		void watch(const std::string & key);
		std::set<std::tuple<std::string,int,timeval_type>> & get_watching() { return watching; }
		size_t get_transaction_size() { return transaction_arguments.size(); }
		bool unqueue();
	private:
		bool parse_line(std::string & line);
		bool parse_data(std::string & data, int size);
	};
	class server_type
	{
		std::shared_ptr<poll_type> poll;
		std::map<socket_type*,std::shared_ptr<client_type>> clients;
		std::shared_ptr<socket_type> listening;
		std::map<std::string,std::string> store;
		std::string password;
		std::vector<database_type> databases;
		bool shutdown;
		static void client_event(socket_type * s, int events);
		static void server_event(socket_type * s, int events);
		void on_client_event(socket_type * s, int events);
		void on_server_event(socket_type * s, int events);
	public:
		server_type();
		bool start(const std::string & hostname, const std::string & port);
		bool execute(client_type * client);
	private:
		typedef bool (server_type::*function_type)(client_type * client);
		std::map<std::string,function_type> function_map;
		void build_function_map();
		//connection api
		bool function_auth(client_type * client);
		bool function_ping(client_type * client);
		bool function_quit(client_type * client);
		bool function_echo(client_type * client);
		bool function_select(client_type * client);
		//server api
		bool function_dbsize(client_type * client);
		bool function_flushall(client_type * client);
		bool function_flushdb(client_type * client);
		bool function_shutdown(client_type * client);
		bool function_time(client_type * client);
		//transaction api
		bool function_multi(client_type * client);
		bool function_exec(client_type * client);
		bool function_discard(client_type * client);
		bool function_watch(client_type * client);
		bool function_unwatch(client_type * client);
	};
}

#endif
