#ifndef INCLUDE_REDIS_CPP_COMMON_H
#define INCLUDE_REDIS_CPP_COMMON_H

#include <functional>
#include <string.h>
#include <string>
#include <vector>
#include <deque>
#include <list>
#include <set>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <tuple>
#include <memory>
#include <exception>
#include <stdexcept>
#include <limits>
#include <algorithm>
#include <stdarg.h>
#include <inttypes.h>
#include <math.h>

namespace rediscpp
{
	inline std::string string_error(int error_number)
	{
		char buf[1024] = {0};
		return std::string(strerror_r(error_number, & buf[0], sizeof(buf)));
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
	inline int64_t atoi64(const std::string & str)
	{
		return strtoll(str.c_str(), NULL, 10);
	}
	inline int64_t atoi64(const std::string & str, bool & is_valid)
	{
		if (str.empty()) {
			is_valid = false;
			return 0;
		}
		char * endptr = NULL;
		errno = 0;
		int64_t result = strtoll(str.c_str(), &endptr, 10);
		const char * end = str.c_str() + str.size();
		is_valid = (endptr == end && errno == 0);
		return result;
	}
	inline long double atold(const std::string & str, bool & is_valid)
	{
		if (str.empty()) {
			is_valid = false;
			return 0;
		}
		char * endptr = NULL;
		errno = 0;
		long double result = strtold(str.c_str(), &endptr);
		const char * end = str.c_str() + str.size();
		is_valid = (endptr == end && errno == 0);
		return result;
	}
	bool pattern_match(const std::string & pattern, const std::string & target, bool nocase = false);
};

#endif
