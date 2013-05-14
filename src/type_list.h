#ifndef INCLUDE_REDIS_CPP_TYPE_LIST_H
#define INCLUDE_REDIS_CPP_TYPE_LIST_H

#include "type_interface.h"

namespace rediscpp
{
	class type_list : public type_interface
	{
		std::list<std::string> value;
		size_t count;
	public:
		type_list();
		type_list(const timeval_type & current);
		virtual ~type_list();
		void move(std::list<std::string> && value, size_t count_);
		virtual type_types get_type() const { return list_type; }
		virtual void output(std::shared_ptr<file_type> & dst) const;
		virtual void output(std::string & dst) const;
		static std::shared_ptr<type_list> input(std::shared_ptr<file_type> & src);
		static std::shared_ptr<type_list> input(std::pair<std::string::const_iterator,std::string::const_iterator> & src);
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
		size_t lrem(int64_t count_, const std::string & target);
		void trim(size_t start, size_t end);
	private:
		std::list<std::string>::iterator get_it_internal(size_t index);
		std::pair<std::list<std::string>::iterator,std::list<std::string>::iterator> get_range_internal(size_t start, size_t end);
	};
};

#endif
