#include "type_string.h"

namespace rediscpp
{
	string_type::string_type(const std::string & string_value_, const timeval_type & current)
		: type_interface(current)
		, string_value(string_value_)
	{
	}
	string_type::string_type(std::string && string_value_, const timeval_type & current)
		: type_interface(current)
		, string_value(std::move(string_value_))
	{
	}
	const std::string & string_type::get()
	{
		return string_value;
	}
	string_type::~string_type()
	{
	}
	std::string string_type::get_type()
	{
		return std::string("string");
	}
	std::string & string_type::ref()
	{
		return string_value;
	}
	void string_type::set(const std::string & str)
	{
		string_value = str;
	}
	int64_t string_type::append(const std::string & str)
	{
		string_value += str;
		return string_value.size();
	}
	int64_t string_type::setrange(size_t offset, const std::string & str)
	{
		size_t new_size = offset + str.size();
		if (string_value.size() < new_size) {
			string_value.resize(new_size);
		}
		std::copy(str.begin(), str.end(), string_value.begin() + offset);
		return string_value.size();
	}
};
