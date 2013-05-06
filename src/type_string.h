#ifndef INCLUDE_REDIS_CPP_TYPE_STRING_H
#define INCLUDE_REDIS_CPP_TYPE_STRING_H

#include "type_interface.h"

namespace rediscpp
{
	class type_string : public type_interface
	{
		std::string string_value;
	public:
		type_string(const std::string & string_value_, const timeval_type & current);
		type_string(std::string && string_value_, const timeval_type & current);
		virtual ~type_string();
		virtual std::string get_type();
		const std::string & get();
		std::string & ref();
		void set(const std::string & str);
		int64_t append(const std::string & str);
		int64_t setrange(size_t offset, const std::string & str);
	};
};

#endif
