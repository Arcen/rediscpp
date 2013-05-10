#ifndef INCLUDE_REDIS_CPP_TYPE_STRING_H
#define INCLUDE_REDIS_CPP_TYPE_STRING_H

#include "type_interface.h"

namespace rediscpp
{
	class type_string : public type_interface
	{
		std::string string_value;
		int64_t int_value;
		bool int_type;
	public:
		type_string(const std::string & string_value_, const timeval_type & current);
		type_string(std::string && string_value_, const timeval_type & current);
		virtual ~type_string();
		virtual std::string get_type() const;
		virtual int get_int_type() const { return 0; }
		std::string get() const;
		std::string & ref();
		void set(const std::string & str);
		int64_t append(const std::string & str);
		int64_t setrange(size_t offset, const std::string & str);
		bool is_int() const { return int_type; }
		int64_t incrby(int64_t value);
	private:
		void to_int();
		void to_str();
	};
};

#endif
