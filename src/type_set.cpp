#include "type_set.h"

namespace rediscpp
{
	set_type::set_type(const timeval_type & current)
		: type_interface(current)
	{
	}
	set_type::~set_type(){}
	std::string set_type::get_type()
	{
		return std::string("set");
	}
	size_t set_type::sadd(const std::vector<std::string*> & members)
	{
		size_t added = 0;
		for (auto it = members.begin(), end = members.end(); it != end; ++it) {
			auto & member = **it;
			if (value.insert(member).second) {
				++added;
			}
		}
		return added;
	}
	size_t set_type::scard() const
	{
		return value.size();
	}
	bool set_type::sismember(const std::string & member) const
	{
		return value.find(member) != value.end();
	}
	std::pair<std::set<std::string>::const_iterator,std::set<std::string>::const_iterator> set_type::smembers() const
	{
		return std::make_pair(value.begin(), value.end());
	}
	size_t set_type::srem(const std::vector<std::string*> & members)
	{
		size_t removed = 0;
		for (auto it = members.begin(), end = members.end(); it != end; ++it) {
			auto & member = **it;
			auto vit = value.find(member);
			if (vit != value.end()) {
				value.erase(vit);
				++removed;
			}
		}
		return removed;
	}
	bool set_type::erase(const std::string & member)
	{
		auto vit = value.find(member);
		if (vit != value.end()) {
			value.erase(vit);
			return true;
		}
		return false;
	}
	bool set_type::insert(const std::string & member)
	{
		return value.insert(member).second;
	}
	std::string set_type::random_key(const std::string & low, const std::string & high)
	{
		if (low.empty() || high.empty() || ! (low < high)) {
			throw std::runtime_error(format("random_key argument [%s] and [%s]", low.c_str(), high.c_str()));
		}
		std::string result = low;
		size_t len = result.size();
		size_t high_len = high.size();
		size_t index = 0;
		size_t min_len = std::min(len, high_len);;
		for (; index < min_len; ++index) {
			if (result[index] != high[index]) {
				break;
			}
		}
		if (index < min_len) {
			//abcd, abs
			uint8_t diff = high[index] - result[index];
			uint8_t r = rand() % (diff + 1);
			if (r == 0) {
				return low;
			}
			result[index] += r;
			result.resize(index + 1);
			return result;
		}
		//abc, abcx
		uint8_t diff = high[index];
		uint8_t r = rand() % (diff + 1);
		if (r) {
			result.push_back(static_cast<char>(r));
		}
		return result;
	}
	std::set<std::string>::const_iterator set_type::srandmember() const
	{
		auto it = value.begin();
		std::advance(it, rand() % value.size());
		return it;
		/*
		if (value.empty()) {
			return value.end();
		}
		auto front = value.begin();
		auto back = value.end();
		--back;
		while (front != back) {
			std::string middle = random_key(*front, *back);
			if (middle == *front) {
				return front;
			}
			if (middle == *back) {
				return back;
			}
			auto it = value.lower_bound(middle);//middle = (front,back), it = (front,back]
			if (it == back) {
				return rand() & 1 ? front : back;
			}
			if (rand() & 1) {
				front = it;
			} else {
				back = it;
			}
		}
		return front;
		/*/
	}
	///重複を許してcount個の要素を選択する
	bool set_type::set_type::srandmember(size_t count, std::vector<std::set<std::string>::const_iterator> & result) const
	{
		result.clear();
		if (count == 0) {
			if (value.empty()) {
				return true;
			}
			count = 1;
		}
		result.reserve(count);
		for (size_t i = 0; i < count; ++i) {
			result.push_back(srandmember());
		}
		return true;
	}
	///重複を許さずにcount個の要素を選択する
	bool set_type::srandmember_distinct(size_t count, std::vector<std::set<std::string>::const_iterator> & result) const
	{
		result.clear();
		if (value.size() < count) {
			return false;
		}
		result.reserve(count);
		auto it = value.begin();
		for (size_t i = 0; i < count; ++i, ++it) {
			result.push_back(it);
		}
		for (size_t i = count, n = value.size(); i < n; ++i, ++it) {
			int pickup = rand() % (i + 1);
			if (pickup < count) {
				result[pickup] = it;
			}
		}
		return true;
	}
	bool set_type::empty() const
	{
		return value.empty();
	}
	size_t set_type::size() const
	{
		return value.size();
	}
	void set_type::clear()
	{
		value.clear();
	}
	void set_type::sunion(const set_type & rhs)
	{
		if (this == &rhs) {
			return;
		}
		value.insert(rhs.value.begin(), rhs.value.end());
	}
	void set_type::sdiff(const set_type & rhs)
	{
		if (this == &rhs) {
			clear();
			return;
		}
		std::set<std::string> lhs;
		lhs.swap(value);
		std::set_difference(lhs.begin(), lhs.end(), rhs.value.begin(), rhs.value.end(), std::inserter(value, value.begin()));
	}
	void set_type::sinter(const set_type & rhs)
	{
		if (this == &rhs) {
			return;
		}
		std::set<std::string> lhs;
		lhs.swap(value);
		std::set_intersection(lhs.begin(), lhs.end(), rhs.value.begin(), rhs.value.end(), std::inserter(value, value.begin()));
	}
};

