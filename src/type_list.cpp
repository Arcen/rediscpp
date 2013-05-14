#include "type_list.h"

namespace rediscpp
{
	type_list::type_list()
		: count(0)
	{
	}
	type_list::type_list(const timeval_type & current)
		: type_interface(current)
		, count(0)
	{
	}
	type_list::~type_list()
	{
	}
	void type_list::move(std::list<std::string> && value, size_t count_)
	{
		value.swap(value);
		count = count_;
	}
	void type_list::lpush(const std::vector<std::string*> & elements)
	{
		for (auto it = elements.begin(), end = elements.end(); it != end; ++it) {
			value.insert(value.begin(), **it);
		}
		count += elements.size();
	}
	void type_list::rpush(const std::vector<std::string*> & elements)
	{
		for (auto it = elements.begin(), end = elements.end(); it != end; ++it) {
			value.insert(value.end(), **it);
		}
		count += elements.size();
	}
	bool type_list::linsert(const std::string & pivot, const std::string & element, bool before)
	{
		auto it = std::find(value.begin(), value.end(), pivot);
		if (it == value.end()) {
			return false;
		}
		if (!before) {
			++it;
		}
		value.insert(it, element);
		++count;
		return true;
	}
	void type_list::lpush(const std::string & element)
	{
		value.push_front(element);
		++count;
	}
	void type_list::rpush(const std::string & element)
	{
		value.push_back(element);
		++count;
	}
	std::string type_list::lpop()
	{
		if (count == 0) {
			throw std::runtime_error("lpop failed. list is empty");
		}
		std::string result = value.front();
		value.pop_front();
		--count;
		return result;
	}
	std::string type_list::rpop()
	{
		if (count == 0) {
			throw std::runtime_error("rpop failed. list is empty");
		}
		std::string result = value.back();
		value.pop_back();
		--count;
		return result;
	}
	size_t type_list::size() const
	{
		return count;
	}
	bool type_list::empty() const
	{
		return count == 0;
	}
	std::list<std::string>::const_iterator type_list::get_it(size_t index) const
	{
		if (count <= index) {
			return value.end();
		}
		if (index <= count / 2) {
			std::list<std::string>::const_iterator it = value.begin();
			for (auto i = 0; i < index; ++i) {
				++it;
			}
			return it;
		}
		std::list<std::string>::const_iterator it = value.end();
		for (auto i = count; index < i; --i) {
			--it;
		}
		return it;
	}
	std::list<std::string>::iterator type_list::get_it_internal(size_t index)
	{
		if (count <= index) {
			return value.end();
		}
		if (index <= count / 2) {
			std::list<std::string>::iterator it = value.begin();
			for (auto i = 0; i < index; ++i) {
				++it;
			}
			return it;
		}
		std::list<std::string>::iterator it = value.end();
		for (auto i = count; index < i; --i) {
			--it;
		}
		return it;
	}
	bool type_list::set(int64_t index, const std::string & newval)
	{
		if (index < 0 || count <= index) return false;
		auto it = get_it_internal(index);
		*it = newval;
		return true;
	}
	std::pair<std::list<std::string>::const_iterator,std::list<std::string>::const_iterator> type_list::get_range(size_t start, size_t end) const
	{
		start = std::min(count, start);
		end = std::min(count, end);
		if (end <= start) {
			return std::make_pair(value.end(), value.end());
		}
		std::list<std::string>::const_iterator sit = get_it(start);
		if (count <= end) {
			return std::make_pair(sit, value.end());
		}
		if (start == end) {
			return std::make_pair(sit, sit);
		}
		std::list<std::string>::const_iterator eit;
		if (end - start < count - end) {
			eit = sit;
			for (size_t i = start; i < end; ++i) {
				++eit;
			}
		} else {
			eit = get_it(end);
		}
		return std::make_pair(sit, eit);
	}
	std::pair<std::list<std::string>::const_iterator,std::list<std::string>::const_iterator> type_list::get_range() const
	{
		return std::make_pair(value.begin(), value.end());
	}
	std::pair<std::list<std::string>::iterator,std::list<std::string>::iterator> type_list::get_range_internal(size_t start, size_t end)
	{
		start = std::min(count, start);
		end = std::min(count, end);
		if (end <= start) {
			return std::make_pair(value.end(), value.end());
		}
		std::list<std::string>::iterator sit = get_it_internal(start);
		if (count <= end) {
			return std::make_pair(sit, value.end());
		}
		if (start == end) {
			return std::make_pair(sit, sit);
		}
		std::list<std::string>::iterator eit;
		if (end - start < count - end) {
			eit = sit;
			for (size_t i = start; i < end; ++i) {
				++eit;
			}
		} else {
			eit = get_it_internal(end);
		}
		return std::make_pair(sit, eit);
	}
	///@param[in] count 0ならすべてを消す、正ならfrontから指定数を消す、負ならbackから指定数を消す
	///@return 削除数
	size_t type_list::lrem(int64_t count_, const std::string & target)
	{
		size_t removed = 0;
		if (count_ == 0) {
			for (auto it = value.begin(); it != value.end();) {
				if (*it == target) {
					it = value.erase(it);
					++removed;
				} else {
					++it;
				}
			}
		} else if (0 < count_) {
			for (auto it = value.begin(); it != value.end();) {
				if (*it == target) {
					it = value.erase(it);
					++removed;
					if (count_ == removed) {
						break;
					}
				} else {
					++it;
				}
			}
		} else {
			count_ = - count_;
			if (!value.empty()) {
				auto it = value.end();
				--it;
				while (true) {
					if (*it == target) {
						it = value.erase(it);
						++removed;
						if (count_ == removed) {
							break;
						}
					}
					if (it == value.begin()) {
						break;
					}
					--it;
				}
			}
		}
		count -= removed;
		return removed;
	}
	///[start,end)の範囲になるように前後を削除する
	void type_list::trim(size_t start, size_t end)
	{
		start = std::min(count, start);
		end = std::min(count, end);
		if (end <= start) {
			value.clear();
			count = 0;
			return;
		}
		auto range = get_range_internal(start, end);
		count = end - start;
		value.erase(value.begin(), range.first);
		value.erase(range.second, value.end());
	}
};
