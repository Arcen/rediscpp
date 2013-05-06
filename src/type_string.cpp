#include "type_string.h"

namespace rediscpp
{
	type_string::type_string(const std::string & string_value_, const timeval_type & current)
		: type_interface(current)
		, string_value(string_value_)
	{
	}
	type_string::type_string(std::string && string_value_, const timeval_type & current)
		: type_interface(current)
		, string_value(std::move(string_value_))
	{
	}
	const std::string & type_string::get()
	{
		return string_value;
	}
	type_string::~type_string()
	{
	}
	std::string type_string::get_type()
	{
		return std::string("string");
	}
	std::string & type_string::ref()
	{
		return string_value;
	}
	void type_string::set(const std::string & str)
	{
		string_value = str;
	}
	int64_t type_string::append(const std::string & str)
	{
		string_value += str;
		return string_value.size();
	}
	int64_t type_string::setrange(size_t offset, const std::string & str)
	{
		size_t new_size = offset + str.size();
		if (string_value.size() < new_size) {
			string_value.resize(new_size);
		}
		std::copy(str.begin(), str.end(), string_value.begin() + offset);
		return string_value.size();
	}
};
