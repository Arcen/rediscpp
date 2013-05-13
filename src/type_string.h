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
		type_string(const std::string & string_value_);
		type_string(std::string && string_value_);
		virtual ~type_string();
		virtual type_types get_type() const { return string_type; }
		virtual void output(std::shared_ptr<file_type> & dst) const;
		virtual void output(std::string & dst) const;
		static std::shared_ptr<type_string> input(std::shared_ptr<file_type> & src);
		static std::shared_ptr<type_string> input(std::pair<std::string::const_iterator,std::string::const_iterator> & src);
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
