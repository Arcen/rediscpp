#ifndef INCLUDE_REDIS_CPP_NETWORK_H
#define INCLUDE_REDIS_CPP_NETWORK_H

#include <stdint.h>

namespace rediscpp
{
	class address_type
	{
		//sockaddr_in addr;
		uint16_t port;
	public:
		address_type();
	};
}

#endif
