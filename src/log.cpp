#include "log.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

namespace rediscpp
{
	void lputs(const char * file, int line, log_levels level, const std::string & msg)
	{
		static const char * levels[] = {
			"emergency",
			"alert",
			"critical",
			"error",
			"warning",
			"notice",
			"info",
			"debug",
		};
		time_t t = time(NULL);
		static bool tzset_called = false;
		if (!tzset_called)
		{
			tzset();
			tzset_called = true;
		}
		tm tm;
		memset(&tm, 0, sizeof(tm));
		char current_time_str[128] = "";
		if (localtime_r(&t, &tm)) {
			strftime(current_time_str, sizeof(current_time_str), "%F %T", &tm);
		}
		printf("%s %s(%d) [%s] %s\n", current_time_str, file, line, levels[level], msg.c_str());
	}
	void lprintf(const char * file, int line, log_levels level, const char * fmt, ...)
	{
		va_list args;
		va_start( args, fmt );
		lvprintf(file, line, level, fmt, args);
		va_end( args );
	}
	void lvprintf(const char * file, int line, log_levels level, const char * fmt, va_list args)
	{
		lputs(file, line, level, vformat( fmt, args ));
	}
}
