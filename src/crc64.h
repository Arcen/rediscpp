#ifndef INCLUDE_REDIS_CPP_CRC64_H
#define INCLUDE_REDIS_CPP_CRC64_H

#include "common.h"
#include <stdarg.h>

namespace rediscpp
{
	namespace crc64
	{
		void initialize();
		uint64_t update(uint64_t crc, const void * buf, size_t len);
	};
};

#endif
