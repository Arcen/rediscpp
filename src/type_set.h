#ifndef INCLUDE_REDIS_CPP_TYPE_SET_H
#define INCLUDE_REDIS_CPP_TYPE_SET_H

#include "type_interface.h"

namespace rediscpp
{
	class type_set : public type_interface
	{
		std::set<std::string> value;
	public:
		type_set();
		type_set(const timeval_type & current);
		virtual ~type_set();
		virtual type_types get_type() const { return set_type; }
		virtual void output(std::shared_ptr<file_type> & dst) const;
		virtual void output(std::string & dst) const;
		static std::shared_ptr<type_set> input(std::shared_ptr<file_type> & src);
		static std::shared_ptr<type_set> input(std::pair<std::string::const_iterator,std::string::const_iterator> & src);
		size_t sadd(const std::vector<std::string*> & members);
		size_t scard() const;
		bool sismember(const std::string & member) const;
		std::pair<std::set<std::string>::const_iterator,std::set<std::string>::const_iterator> smembers() const;
		size_t srem(const std::vector<std::string*> & members);
		bool erase(const std::string & member);
		bool insert(const std::string & member);
	private:
		static std::string random_key(const std::string & low, const std::string & high);
	public:
		std::set<std::string>::const_iterator srandmember() const;
		bool srandmember(size_t count, std::vector<std::set<std::string>::const_iterator> & result) const;
		bool srandmember_distinct(size_t count, std::vector<std::set<std::string>::const_iterator> & result) const;
		bool empty() const;
		size_t size() const;
		void clear();
		void sunion(const type_set & rhs);
		void sdiff(const type_set & rhs);
		void sinter(const type_set & rhs);
	};
};

#endif
