#ifndef INCLUDE_REDIS_CPP_SERVER_H
#define INCLUDE_REDIS_CPP_SERVER_H

#include "network.h"
#include <list>

namespace rediscpp
{
	class server_type;
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
	public:
		client_type(std::shared_ptr<socket_type> client_, const std::string & password_)
			: client(client_)
			, argument_count(0)
			, argument_index(0)
			, argument_size(argument_is_undefined)
			, password(password_)
			, db_index(0)
		{
		}
		bool parse(server_type * server);
		arguments_type & get_arguments() { return arguments; }
		void response_status(const std::string & state);
		void response_error(const std::string & state);
		void response_ok();
		void response_pong();
		void response_integer0();
		void response_integer1();
		void response_bulk(const std::string & bulk, bool not_null = true);
		void close_after_send() { client->close_after_send(); }
		bool require_auth(const std::string & auth);
		bool auth(const std::string & password_);
		void select(int index) { db_index = index; }
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
		int max_db;
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
	};
}

#endif
