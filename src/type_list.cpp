#include "type_list.h"

namespace rediscpp
{
	type_list::type_list(const timeval_type & current)
		: type_interface(current)
		, size_(0)
	{
	}
	type_list::type_list(std::list<std::string> && value_, const timeval_type & current)
		: type_interface(current)
		, value(std::move(value_))
		, size_(value.size())
	{
	}
	type_list::~type_list()
	{
	}
	std::string type_list::get_type()
	{
		return std::string("list");
	}
	void type_list::lpush(const std::vector<std::string*> & elements)
	{
		for (auto it = elements.begin(), end = elements.end(); it != end; ++it) {
			value.insert(value.begin(), **it);
		}
		size_ += elements.size();
	}
	void type_list::rpush(const std::vector<std::string*> & elements)
	{
		for (auto it = elements.begin(), end = elements.end(); it != end; ++it) {
			value.insert(value.end(), **it);
		}
		size_ += elements.size();
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
		++size_;
		return true;
	}
	void type_list::lpush(const std::string & element)
	{
		value.push_front(element);
		++size_;
	}
	void type_list::rpush(const std::string & element)
	{
		value.push_back(element);
		++size_;
	}
	std::string type_list::lpop()
	{
		if (size_ == 0) {
			throw std::runtime_error("lpop failed. list is empty");
		}
		std::string result = value.front();
		value.pop_front();
		--size_;
		return result;
	}
	std::string type_list::rpop()
	{
		if (size_ == 0) {
			throw std::runtime_error("rpop failed. list is empty");
		}
		std::string result = value.back();
		value.pop_back();
		--size_;
		return result;
	}
	size_t type_list::size() const
	{
		return size_;
	}
	bool type_list::empty() const
	{
		return size_ == 0;
	}
	std::list<std::string>::const_iterator type_list::get_it(size_t index) const
	{
		if (size_ <= index) {
			return value.end();
		}
		if (index <= size_ / 2) {
			std::list<std::string>::const_iterator it = value.begin();
			for (auto i = 0; i < index; ++i) {
				++it;
			}
			return it;
		}
		std::list<std::string>::const_iterator it = value.end();
		for (auto i = size_; index < i; --i) {
			--it;
		}
		return it;
	}
	std::list<std::string>::iterator type_list::get_it_internal(size_t index)
	{
		if (size_ <= index) {
			return value.end();
		}
		if (index <= size_ / 2) {
			std::list<std::string>::iterator it = value.begin();
			for (auto i = 0; i < index; ++i) {
				++it;
			}
			return it;
		}
		std::list<std::string>::iterator it = value.end();
		for (auto i = size_; index < i; --i) {
			--it;
		}
		return it;
	}
	bool type_list::set(int64_t index, const std::string & newval)
	{
		if (index < 0 || size_ <= index) return false;
		auto it = get_it_internal(index);
		*it = newval;
		return true;
	}
	std::pair<std::list<std::string>::const_iterator,std::list<std::string>::const_iterator> type_list::get_range(size_t start, size_t end) const
	{
		start = std::min(size_, start);
		end = std::min(size_, end);
		if (end <= start) {
			return std::make_pair(value.end(), value.end());
		}
		std::list<std::string>::const_iterator sit = get_it(start);
		if (size_ <= end) {
			return std::make_pair(sit, value.end());
		}
		if (start == end) {
			return std::make_pair(sit, sit);
		}
		std::list<std::string>::const_iterator eit;
		if (end - start < size_ - end) {
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
		start = std::min(size_, start);
		end = std::min(size_, end);
		if (end <= start) {
			return std::make_pair(value.end(), value.end());
		}
		std::list<std::string>::iterator sit = get_it_internal(start);
		if (size_ <= end) {
			return std::make_pair(sit, value.end());
		}
		if (start == end) {
			return std::make_pair(sit, sit);
		}
		std::list<std::string>::iterator eit;
		if (end - start < size_ - end) {
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
	size_t type_list::lrem(int64_t count, const std::string & target)
	{
		size_t removed = 0;
		if (count == 0) {
			for (auto it = value.begin(); it != value.end();) {
				if (*it == target) {
					it = value.erase(it);
					++removed;
				} else {
					++it;
				}
			}
		} else if (0 < count) {
			for (auto it = value.begin(); it != value.end();) {
				if (*it == target) {
					it = value.erase(it);
					++removed;
					if (count == removed) {
						break;
					}
				} else {
					++it;
				}
			}
		} else {
			count = - count;
			if (!value.empty()) {
				auto it = value.end();
				--it;
				while (true) {
					if (*it == target) {
						it = value.erase(it);
						++removed;
						if (count == removed) {
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
		size_ -= removed;
		return removed;
	}
	///[start,end)の範囲になるように前後を削除する
	void type_list::trim(size_t start, size_t end)
	{
		start = std::min(size_, start);
		end = std::min(size_, end);
		if (end <= start) {
			value.clear();
			size_ = 0;
			return;
		}
		auto range = get_range_internal(start, end);
		size_ = end - start;
		value.erase(value.begin(), range.first);
		value.erase(range.second, value.end());
	}
};
