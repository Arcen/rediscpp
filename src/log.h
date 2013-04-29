#ifndef INCLUDE_REDIS_CPP_LOG_H
#define INCLUDE_REDIS_CPP_LOG_H

#include "common.h"
#include <stdarg.h>

namespace rediscpp
{
	enum log_levels
	{
		emergency_level,
		alert_level,
		critical_level,
		error_level,
		warning_level,
		notice_level,
		info_level,
		debug_level,
	};
	void lputs(const char * file, int line, log_levels level, const std::string & msg);
	void lprintf(const char * file, int line, log_levels level, const char * fmt, ...);
	void lvprintf(const char * file, int line, log_levels level, const char * fmt, va_list args);
};

#endif
