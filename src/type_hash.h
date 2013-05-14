#ifndef INCLUDE_REDIS_CPP_TYPE_HASH_H
#define INCLUDE_REDIS_CPP_TYPE_HASH_H

#include "type_interface.h"

namespace rediscpp
{
	class type_hash : public type_interface
	{
		std::unordered_map<std::string, std::string> value;
	public:
		type_hash();
		type_hash(const timeval_type & current);
		virtual ~type_hash();
		virtual type_types get_type() const { return hash_type; }
		virtual void output(std::shared_ptr<file_type> & dst) const;
		virtual void output(std::string & dst) const;
		static std::shared_ptr<type_hash> input(std::shared_ptr<file_type> & src);
		static std::shared_ptr<type_hash> input(std::pair<std::string::const_iterator,std::string::const_iterator> & src);
		size_t hdel(const std::vector<std::string*> & fields);
		bool hexists(const std::string field) const;
		bool empty() const;
		std::pair<std::string,bool> hget(const std::string field) const;
		std::pair<std::unordered_map<std::string, std::string>::const_iterator,std::unordered_map<std::string, std::string>::const_iterator> hgetall() const;
		size_t size() const;
		bool hset(const std::string & field, const std::string & val, bool nx = false);
	};
};

#endif
