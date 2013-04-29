#ifndef INCLUDE_REDIS_CPP_SERVER_H
#define INCLUDE_REDIS_CPP_SERVER_H

#include "network.h"
#include <list>

namespace rediscpp
{
	class client_type
	{
		std::shared_ptr<socket_type> client;
		std::list<std::pair<std::string,bool>> arguments;
		int argument_count;
		int argument_index;
		int argument_size;
		static const int argument_is_null = -1;
		static const int argument_is_undefined = -2;
	public:
		client_type(std::shared_ptr<socket_type> client_)
			: client(client_)
			, argument_count(0)
			, argument_index(0)
			, argument_size(argument_is_undefined)
		{
		}
		bool parse();
	private:
		bool parse_line(std::string & line);
		bool parse_data(std::string & data, int size);
		bool execute();
	};
	class server_type
	{
		std::shared_ptr<poll_type> poll;
		std::map<socket_type*,std::shared_ptr<client_type>> clients;
		std::shared_ptr<socket_type> listening;
		static void client_event(socket_type * s, int events);
		static void server_event(socket_type * s, int events);
		void on_client_event(socket_type * s, int events);
		void on_server_event(socket_type * s, int events);
	public:
		server_type();
		bool start(const std::string & hostname, const std::string & port);
	};
}

#endif
