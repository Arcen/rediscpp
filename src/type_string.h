#ifndef INCLUDE_REDIS_CPP_TYPE_STRING_H
#define INCLUDE_REDIS_CPP_TYPE_STRING_H

#include "type_interface.h"

namespace rediscpp
{
	class string_type : public type_interface
	{
		std::string string_value;
	public:
		string_type(const std::string & string_value_, const timeval_type & current);
		string_type(std::string && string_value_, const timeval_type & current);
		virtual ~string_type();
		virtual std::string get_type();
		const std::string & get();
		std::string & ref();
		void set(const std::string & str);
		int64_t append(const std::string & str);
		int64_t setrange(size_t offset, const std::string & str);
	};
};

#endif
