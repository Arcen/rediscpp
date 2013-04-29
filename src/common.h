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

namespace rediscpp
{
	inline std::string string_error(int error_number)
	{
		std::vector<char> buf(1024, '\0');
		strerror_r(error_number, & buf[0], buf.size());
		return std::string(&buf[0]);
	}
};

#endif
