#ifndef INCLUDE_REDIS_CPP_CRC64_H
#define INCLUDE_REDIS_CPP_CRC64_H

#include "common.h"
#include <stdarg.h>

namespace rediscpp
{
	class crc64_type
	{
		static uint64_t table[256];
		static bool initialized;
	public:
		crc64_type();
		uint64_t update(uint64_t crc, const void * buf, size_t len);
	};
};

#endif
