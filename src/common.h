#ifndef INCLUDE_REDIS_CPP_COMMON_H
#define INCLUDE_REDIS_CPP_COMMON_H

#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif

#include <endian.h>

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
#include <iterator>
#include <stdarg.h>
#include <inttypes.h>
#include <math.h>

namespace rediscpp
{
	std::string string_error(int error_number);
	std::string vformat(const char * fmt, va_list args);
	std::string format(const char * fmt, ...);
	int64_t atoi64(const std::string & str);
	int64_t atoi64(const std::string & str, bool & is_valid);
	uint16_t atou16(const std::string & str, bool & is_valid);
	long double atold(const std::string & str, bool & is_valid);
	double atod(const std::string & str, bool & is_valid);
	bool pattern_match(const std::string & pattern, const std::string & target, bool nocase = false);
};

#endif
