#include "common.h"

namespace rediscpp
{
	std::string string_error(int error_number)
	{
		char buf[1024] = {0};
		return std::string(strerror_r(error_number, & buf[0], sizeof(buf)));
	}
	std::string vformat(const char * fmt, va_list args)
	{
		char buf[1024];
		int ret = vsnprintf(buf, sizeof(buf), fmt, args);
		if (0 <= ret && static_cast<size_t>( ret ) + 1 < sizeof(buf)) return std::string(buf);
		std::vector<char> buf2( ret + 16, 0 );
		ret = vsnprintf(&buf2[0], buf2.size(), fmt, args);
		if (0 <= ret && static_cast<size_t>( ret ) + 1 < buf2.size()) return std::string(&buf2[0]);
		return std::string();
	}
	std::string format(const char * fmt, ...)
	{
		va_list args;
		va_start( args, fmt );
		std::string ret = vformat( fmt, args );
		va_end( args );
		return ret;
	}
	int64_t atoi64(const std::string & str)
	{
		return strtoll(str.c_str(), NULL, 10);
	}
	int64_t atoi64(const std::string & str, bool & is_valid)
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
	uint16_t atou16(const std::string & str, bool & is_valid)
	{
		if (str.empty()) {
			is_valid = false;
			return 0;
		}
		char * endptr = NULL;
		errno = 0;
		int32_t result = strtoul(str.c_str(), &endptr, 10);
		const char * end = str.c_str() + str.size();
		is_valid = (endptr == end && errno == 0 && 0 <= result && result <= 0xFFFF);
		return static_cast<uint16_t>(result);
	}
	long double atold(const std::string & str, bool & is_valid)
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
	double atod(const std::string & str, bool & is_valid)
	{
		if (str.empty()) {
			is_valid = false;
			return 0;
		}
		char * endptr = NULL;
		errno = 0;
		long double result = strtod(str.c_str(), &endptr);
		const char * end = str.c_str() + str.size();
		is_valid = (endptr == end && errno == 0);
		return result;
	}
	static bool pattern_match(const char * pbegin, const char * pend, const char * tbegin, const char * tend, bool nocase)
	{
		while (pbegin < pend)
		{
			switch (*pbegin)
			{
			case '*':
				for (const char * t = tend; tbegin <= t; --t) {
					if (pattern_match(pbegin + 1, pend, t, tend, nocase)) {
						return true;
					}
				}
				return false;
			case '?':
				if (tbegin == tend) {
					return false;
				}
				++tbegin;
				++pbegin;
				break;
			case '[':
				{
					if (tbegin == tend) {
						return false;
					}
					char c = *tbegin;
					if (nocase) {
						c = toupper(c);
					}
					++pbegin;
					bool bend = false;
					bool match = false;
					bool should_not_match = false;
					if (pbegin < pend && *pbegin == '^') {
						should_not_match = true;
						++pbegin;
					}
					const char * bfirst = pbegin;
					for (; pbegin < pend && !bend; ++pbegin)
					{
						switch (*pbegin)
						{
						case ']':
							bend = true;
							break;
						case '\\':
							++pbegin;
							if (pbegin == pend) {
								return false;
							}
							if ((nocase ? toupper(*pbegin) : *pbegin) == c) {
								match = true;
							}
							break;
						case '-':
							if (bfirst == pbegin || pbegin + 1 == pend || pbegin[1] == ']') {
								if ('-' == c) {
									match = true;
								}
							} else {
								char start = pbegin[-1];
								char end = pbegin[1];
								if (nocase) {
									start = toupper(start);
									end = toupper(end);
								}
								if (end < start) std::swap(start, end);
								if (start <= c && c <= end) {
									match = true;
								}
							}
							break;
						default:
							if ((nocase ? toupper(*pbegin) : *pbegin) == c) {
								match = true;
							}
							break;
						}
					}
					if (should_not_match) {
						match = ! match;
					}
					if (!match) {
						return false;
					}
					++tbegin;
				}
				break;
			case '\\':
				if (pbegin + 1 < pend) {
					++pbegin;
				}
			default:
				if (tbegin == tend) {
					return false;
				}
				if (nocase) {
					if (toupper(*pbegin) != toupper(*tbegin)) {
						return false;
					}
				} else {
					if (*pbegin != *tbegin) {
						return false;
					}
				}
				++tbegin;
				++pbegin;
				break;
			}
		}
		return tbegin == tend;
	}
	bool pattern_match(const std::string & pattern, const std::string & target, bool nocase)
	{
		return pattern_match(pattern.c_str(), pattern.c_str() + pattern.size(), target.c_str(), target.c_str() + target.size(), nocase);
	}
}
