#include "type_zset.h"
#include "log.h"

namespace rediscpp
{
	zset_type::value_type::value_type()
		: score(0)
	{
	}
	zset_type::value_type::value_type(const value_type & rhs)
		: member(rhs.member)
		, score(rhs.score)
	{
	}
	zset_type::value_type::value_type(const std::string & member_)
		: member(member_)
		, score(0)
	{
	}
	zset_type::value_type::value_type(const std::string & member_, score_type score_)
		: member(member_)
		, score(score_)
	{
	}
	bool zset_type::score_eq(score_type lhs, score_type rhs)
	{
		if (::finite(lhs) && ::finite(rhs)) {
			return lhs == rhs;
		}
		if (isnan(lhs) || isnan(rhs)) {
			return false;
		}
		if (isinf(lhs) && isinf(rhs)) {
			return (lhs < 0) == (rhs < 0);
		}
		return false;
	}
	bool zset_type::score_less(score_type lhs, score_type rhs)
	{
		if (::finite(lhs) && ::finite(rhs)) {
			return lhs < rhs;
		}
		if (isnan(lhs) || isnan(rhs)) {
			return false;
		}
		if (isinf(lhs) && isinf(rhs)) {
			return lhs < 0 && 0 < rhs;
		}
		if (isinf(lhs)) {
			return lhs < 0;//-inf < not -inf
		}
		if (isinf(rhs)) {
			return 0 < rhs;//not inf < +inf
		}
		return false;
	}
	bool zset_type::score_comparer::operator()(const std::shared_ptr<value_type> & lhs, const std::shared_ptr<value_type> & rhs) const
	{
		score_type ls = lhs->score;
		score_type rs = rhs->score;
		if (!score_eq(ls, rs)) {
			return score_less(ls, rs);
		}
		return lhs->member < rhs->member;
	}
	zset_type::zset_type(const timeval_type & current)
		: type_interface(current)
	{
	}
	zset_type::~zset_type()
	{
	}
	std::string zset_type::get_type()
	{
		return std::string("zset");
	}
	bool zset_type::erase_sorted(const std::shared_ptr<value_type> & rhs)
	{
		auto it = sorted.find(rhs);
		if (it != sorted.end()) {
			sorted.erase(it);
			return true;
		}
		lprintf(__FILE__, __LINE__, alert_level, "zset structure broken, %s not found value by score %f", rhs->member.c_str(), rhs->score);
		return false;
	}
	size_t zset_type::zadd(const std::vector<score_type> & scores, const std::vector<std::string*> & members)
	{
		if (scores.size() != members.size()) {
			return 0;
		}
		size_t created = 0;
		auto sit = scores.begin();
		for (auto it = members.begin(), end = members.end(); it != end; ++it, ++sit) {
			auto & member = **it;
			auto score = *sit;
			std::shared_ptr<value_type> v(new value_type(member, score));
			auto vit = value.find(member);
			if (vit == value.end()) {
				++created;
				value.insert(std::make_pair(member, v));
				sorted.insert(v);
			} else {
				auto old = vit->second;
				erase_sorted(old);
				vit->second = v;
				sorted.insert(v);
			}
		}
		return created;
	}
	size_t zset_type::zrem(const std::vector<std::string*> & members)
	{
		size_t removed = 0;
		for (auto it = members.begin(), end = members.end(); it != end; ++it) {
			auto & member = **it;
			auto vit = value.find(member);
			if (vit != value.end()) {
				erase_sorted(vit->second);
				value.erase(vit);
				++removed;
			}
		}
		return removed;
	}
	size_t zset_type::zcard() const
	{
		return value.size();
	}
	size_t zset_type::size() const
	{
		return value.size();
	}
	bool zset_type::empty() const
	{
		return value.empty();
	}
	void zset_type::clear()
	{
		value.clear();
		sorted.clear();
	}
	std::pair<zset_type::const_iterator,zset_type::const_iterator> zset_type::zrangebyscore(score_type minimum, score_type maximum, bool inclusive_minimum, bool inclusive_maximum) const
	{
		if (maximum < minimum) {
			return std::make_pair(sorted.end(), sorted.end());
		}
		std::shared_ptr<value_type> min_value(new value_type(std::string(), minimum));
		std::shared_ptr<value_type> max_value(new value_type(std::string(), maximum));
		auto first = sorted.lower_bound(min_value);
		auto last = sorted.lower_bound(max_value);
		if (!inclusive_minimum) {
			while (first != sorted.end()) {
				if (!score_eq((*first)->score, minimum)) {
					break;
				}
				++first;
			}
		}
		if (inclusive_maximum) {
			while (last != sorted.end()) {
				if (score_eq((*last)->score, maximum)) {
					++last;
				} else {
					break;
				}
			}
		}
		return std::make_pair(first, last);
	}
	std::pair<zset_type::const_iterator,zset_type::const_iterator> zset_type::zrange(size_t start, size_t stop) const
	{
		if (stop <= start) {
			return std::make_pair(sorted.end(), sorted.end());
		}
		return std::make_pair(get_it(start), get_it(stop));
	}
	std::pair<zset_type::const_iterator,zset_type::const_iterator> zset_type::zrange() const
	{
		return std::make_pair(sorted.begin(), sorted.end());
	}
	zset_type::const_iterator zset_type::get_it(size_t index) const
	{
		if (sorted.size() <= index) {
			return sorted.end();
		}
		if (index <= sorted.size() / 2) {
			const_iterator it = sorted.begin();
			for (auto i = 0; i < index; ++i) {
				++it;
			}
			return it;
		}
		const_iterator it = sorted.end();
		for (auto i = sorted.size(); index < i; --i) {
			--it;
		}
		return it;
	}
	size_t zset_type::zcount(score_type minimum, score_type maximum, bool inclusive_minimum, bool inclusive_maximum) const
	{
		auto range = zrangebyscore(minimum, maximum, inclusive_minimum, inclusive_maximum);
		return std::distance(range.first, range.second);
	}
	bool zset_type::zrank(const std::string & member, size_t & rank, bool rev) const
	{
		auto it = value.find(member);
		if (it == value.end()) {
			return false;
		}
		auto sit = sorted.find(it->second);
		rank = std::distance(sorted.begin(), sit);
		if (rev) {
			rank = value.size() - 1 - rank;
		}
		return true;
	}
	bool zset_type::zscore(const std::string & member, score_type & score) const
	{
		auto it = value.find(member);
		if (it == value.end()) {
			return false;
		}
		score = it->second->score;
		return true;
	}
	///@retval nan ���f
	zset_type::score_type zset_type::zincrby(const std::string & member, score_type increment)
	{
		if (isnan(increment)) {
			return increment;
		}
		auto it = value.find(member);
		if (it == value.end()) {
			std::shared_ptr<value_type> v(new value_type(member, increment));
			value.insert(std::make_pair(member, v));
			sorted.insert(v);
			return increment;
		}
		score_type after = (it->second->score + increment);
		if (isnan(after)) {
			return after;
		}
		erase_sorted(it->second);
		it->second->score = after;
		sorted.insert(it->second);
		return after;
	}
	///�a�W��
	void zset_type::zunion(const zset_type & rhs, zset_type::score_type weight, aggregate_types aggregate)
	{
		if (this == &rhs) {
			return;
		}
		if (rhs.empty()) {
			return;
		}
		auto lit = value.begin(), lend = value.end();
		auto rit = rhs.value.begin(), rend = rhs.value.end();
		while (lit != lend && rit != rend) {
			if (lit->first == rit->first) {//union
				score_type after = lit->second->score;
				switch (aggregate) {
				case aggregate_min:
					after = std::min(after, rit->second->score * weight);
					break;
				case aggregate_max:
					after = std::max(after, rit->second->score * weight);
					break;
				case aggregate_sum:
					after += rit->second->score * weight;
					break;
				}
				if (isnan(after)) {
					throw std::runtime_error("ERR nan score result found");
				}
				erase_sorted(lit->second);
				lit->second->score = after;
				sorted.insert(lit->second);
				++lit;
				++rit;
			} else if (lit->first < rit->first) {//only left
				++lit;
			} else {//only right, insert
				std::shared_ptr<value_type> v(new value_type(*(rit->second)));
				score_type after = v->score * weight;
				if (isnan(after)) {
					throw std::runtime_error("ERR nan score result found");
				}
				v->score = after;
				value.insert(lit, std::make_pair(v->member, v));
				sorted.insert(v);
				++rit;
			}
		}
		while (rit != rend) {//insert
			std::shared_ptr<value_type> v(new value_type(*(rit->second)));
			v->score *= weight;
			if (isnan(v->score)) {
				throw std::runtime_error("ERR nan score result found");
			}
			value.insert(lit, std::make_pair(v->member, v));
			sorted.insert(v);
			++rit;
		}
	}
	///�ϏW��
	void zset_type::zinter(const zset_type & rhs, score_type weight, aggregate_types aggregate)
	{
		if (this == &rhs) {
			return;
		}
		if (empty()) {
			return;
		}
		if (rhs.empty()) {
			clear();
			return;
		}
		auto lit = value.begin();
		auto rit = rhs.value.begin(), rend = rhs.value.end();
		while (lit != value.end() && rit != rend) {
			if (lit->first == rit->first) {//inter
				score_type after = lit->second->score;
				switch (aggregate) {
				case aggregate_min:
					after = std::min(after, rit->second->score * weight);
					break;
				case aggregate_max:
					after = std::max(after, rit->second->score * weight);
					break;
				case aggregate_sum:
					after += rit->second->score * weight;
					break;
				}
				if (isnan(after)) {
					throw std::runtime_error("ERR nan score result found");
				}
				erase_sorted(lit->second);
				lit->second->score = after;
				sorted.insert(lit->second);
				++lit;
				++rit;
			} else if (lit->first < rit->first) {//only left, erase
				erase_sorted(lit->second);
				auto eit = lit;
				++lit;
				value.erase(eit);
			} else {//only right
				++rit;
			}
		}
		for (auto it = lit; it != value.end(); ++it) {//erase
			erase_sorted(it->second);
		}
		value.erase(lit, value.end());
	}
};
