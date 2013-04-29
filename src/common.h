#ifndef INCLUDE_REDIS_CPP_COMMON_H
#define INCLUDE_REDIS_CPP_COMMON_H

#include <functional>
#include <string.h>
#include <string>
#include <vector>
#include <deque>
#include <set>
#include <map>
#include <memory>
#include <exception>
#include <stdexcept>
#include <stdarg.h>

namespace rediscpp
{
	inline std::string string_error(int error_number)
	{
		std::vector<char> buf(1024, '\0');
		strerror_r(error_number, & buf[0], buf.size());
		return std::string(&buf[0]);
	}
	inline std::string vformat(const char * fmt, va_list args)
	{
		char buf[1024];
		int ret = vsnprintf(buf, sizeof(buf), fmt, args);
		if (0 <= ret && static_cast<size_t>( ret ) + 1 < sizeof(buf)) return std::string(buf);
		std::vector<char> buf2( ret + 16, 0 );
		ret = vsnprintf(&buf2[0], buf2.size(), fmt, args);
		if (0 <= ret && static_cast<size_t>( ret ) + 1 < buf2.size()) return std::string(&buf2[0]);
		return std::string();
	}
	inline std::string format( const char * fmt, ... )
	{
		va_list args;
		va_start( args, fmt );
		std::string ret = vformat( fmt, args );
		va_end( args );
		return ret;
	}
};

#endif
