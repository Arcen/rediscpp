#ifndef INCLUDE_REDIS_CPP_SERVER_H
#define INCLUDE_REDIS_CPP_SERVER_H

#include "network.h"
#include <list>

namespace rediscpp
{
	class server_type
	{
		std::shared_ptr<poll_type> poll;
		std::list<std::shared_ptr<socket_type>> clients;
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
