#include "type_string.h"

namespace rediscpp
{
	type_string::type_string(const std::string & string_value_)
		: string_value(string_value_)
		, int_value(0)
		, int_type(false)
	{
	}
	type_string::type_string(std::string && string_value_)
		: string_value(std::move(string_value_))
		, int_value(0)
		, int_type(false)
	{
	}
	std::string type_string::get() const
	{
		if (int_type && string_value.empty()) {
			return format("%"PRId64, int_value);
		}
		return string_value;
	}
	type_string::~type_string()
	{
	}
	std::string & type_string::ref()
	{
		to_str();
		return string_value;
	}
	void type_string::set(const std::string & str)
	{
		string_value = str;
		int_type = false;
	}
	int64_t type_string::append(const std::string & str)
	{
		to_str();
		string_value += str;
		return string_value.size();
	}
	int64_t type_string::setrange(size_t offset, const std::string & str)
	{
		to_str();
		size_t new_size = offset + str.size();
		if (string_value.size() < new_size) {
			string_value.resize(new_size);
		}
		std::copy(str.begin(), str.end(), string_value.begin() + offset);
		return string_value.size();
	}
	void type_string::to_int()
	{
		if (!int_type) {
			int_value = atoi64(string_value, int_type);
		}
	}
	void type_string::to_str()
	{
		if (int_type) {
			string_value = format("%"PRId64, int_value);
			int_type = false;
		}
	}
	int64_t type_string::incrby(int64_t count)
	{
		to_int();
		if (!int_type) {
			throw std::runtime_error("ERR not valid integer");
		}
		if (count < 0) {
			if (int_value < int_value + count) {
				throw std::runtime_error("ERR underflow");
			}
		} else if (0 < count) {
			if (int_value + count < int_value) {
				throw std::runtime_error("ERR overflow");
			}
		}
		int_value += count;
		return int_value;
	}
};
