#include "type_hash.h"

namespace rediscpp
{
	type_hash::type_hash(const timeval_type & current)
		: type_interface(current)
	{
	}
	type_hash::~type_hash()
	{
	}
	std::string type_hash::get_type()
	{
		return std::string("hash");
	}
	size_t type_hash::hdel(const std::vector<std::string*> & fields)
	{
		size_t removed = 0;
		for (auto it = fields.begin(), end = fields.end(); it != end; ++it) {
			auto & field = **it;
			auto vit = value.find(field);
			if (vit != value.end()) {
				value.erase(vit);
				++removed;
			}
		}
		return removed;
	}
	bool type_hash::hexists(const std::string field) const
	{
		return value.find(field) != value.end();
	}
	bool type_hash::empty() const
	{
		return value.empty();
	}
	std::pair<std::string,bool> type_hash::hget(const std::string field) const
	{
		auto it = value.find(field);
		if (it != value.end()) {
			return std::make_pair(it->second, true);
		}
		return std::make_pair(std::string(), false);
	}
	std::pair<std::unordered_map<std::string, std::string>::const_iterator,std::unordered_map<std::string, std::string>::const_iterator> type_hash::hgetall() const
	{
		return std::make_pair(value.begin(), value.end());
	}
	size_t type_hash::size() const
	{
		return value.size();
	}
	bool type_hash::hset(const std::string & field, const std::string & val, bool nx)
	{
		auto it = value.find(field);
		if (it != value.end()) {
			if (nx) {
				return false;
			}
			it->second = val;
			return false;
		} else {
			value[field] = val;
			return true;//created
		}
	}
};
