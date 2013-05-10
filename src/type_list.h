#ifndef INCLUDE_REDIS_CPP_TYPE_LIST_H
#define INCLUDE_REDIS_CPP_TYPE_LIST_H

#include "type_interface.h"

namespace rediscpp
{
	class type_list : public type_interface
	{
		std::list<std::string> value;
		size_t size_;
	public:
		type_list(const timeval_type & current);
		type_list(std::list<std::string> && value_, const timeval_type & current);
		virtual ~type_list();
		virtual std::string get_type() const;
		virtual int get_int_type() const { return 1; }
		void lpush(const std::vector<std::string*> & elements);
		void rpush(const std::vector<std::string*> & elements);
		bool linsert(const std::string & pivot, const std::string & element, bool before);
		void lpush(const std::string & element);
		void rpush(const std::string & element);
		std::string lpop();
		std::string rpop();
		size_t size() const;
		bool empty() const;
		std::list<std::string>::const_iterator get_it(size_t index) const;
		bool set(int64_t index, const std::string & newval);
		std::pair<std::list<std::string>::const_iterator,std::list<std::string>::const_iterator> get_range(size_t start, size_t end) const;
		std::pair<std::list<std::string>::const_iterator,std::list<std::string>::const_iterator> get_range() const;
		size_t lrem(int64_t count, const std::string & target);
		void trim(size_t start, size_t end);
	private:
		std::list<std::string>::iterator get_it_internal(size_t index);
		std::pair<std::list<std::string>::iterator,std::list<std::string>::iterator> get_range_internal(size_t start, size_t end);
	};
};

#endif
