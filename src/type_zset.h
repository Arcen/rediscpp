#ifndef INCLUDE_REDIS_CPP_TYPE_ZSET_H
#define INCLUDE_REDIS_CPP_TYPE_ZSET_

#include "type_interface.h"

namespace rediscpp
{
	class type_zset : public type_interface
	{
	public:
		typedef double score_type;
		enum aggregate_types
		{
			aggregate_min = -1,
			aggregate_sum,
			aggregate_max,
		};
	private:
		struct value_type
		{
			std::string member;
			score_type score;
			value_type();
			value_type(const value_type & rhs);
			value_type(const std::string & member_);
			value_type(const std::string & member_, score_type score_);
		};
		static bool score_eq(score_type lhs, score_type rhs);
		static bool score_less(score_type lhs, score_type rhs);
		struct score_comparer
		{
			bool operator()(const std::shared_ptr<value_type> & lhs, const std::shared_ptr<value_type> & rhs) const;
		};
		std::map<std::string, std::shared_ptr<value_type>> value;//値でユニークな集合
		std::set<std::shared_ptr<value_type>, score_comparer> sorted;//スコアで並べた状態
	public:
		typedef std::set<std::shared_ptr<value_type>, score_comparer>::const_iterator const_iterator;
		type_zset(const timeval_type & current);
		virtual ~type_zset();
		virtual std::string get_type() const;
		virtual int get_int_type() const { return 3; }
		size_t zadd(const std::vector<score_type> & scores, const std::vector<std::string*> & members);
		size_t zrem(const std::vector<std::string*> & members);
		size_t zcard() const;
		size_t size() const;
		bool empty() const;
		void clear();
		std::pair<const_iterator,const_iterator> zrangebyscore(score_type minimum, score_type maximum, bool inclusive_minimum, bool inclusive_maximum) const;
		std::pair<const_iterator,const_iterator> zrange(size_t start, size_t stop) const;
		std::pair<const_iterator,const_iterator> zrange() const;
		size_t zcount(score_type minimum, score_type maximum, bool inclusive_minimum, bool inclusive_maximum) const;
		bool zrank(const std::string & member, size_t & rank, bool rev) const;
		bool zscore(const std::string & member, score_type & score) const;
		score_type zincrby(const std::string & member, score_type increment);
		void zunion(const type_zset & rhs, type_zset::score_type weight, aggregate_types aggregate);
		void zinter(const type_zset & rhs, score_type weight, aggregate_types aggregate);
	private:
		const_iterator get_it(size_t index) const;
		bool erase_sorted(const std::shared_ptr<value_type> & rhs);
	};
};

#endif
