#include "common.h"

namespace rediscpp
{
	bool pattern_match(const char * pbegin, const char * pend, const char * tbegin, const char * tend, bool nocase)
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
