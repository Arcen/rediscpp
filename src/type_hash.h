#ifndef INCLUDE_REDIS_CPP_TYPE_HASH_H
#define INCLUDE_REDIS_CPP_TYPE_HASH_H

#include "type_interface.h"

namespace rediscpp
{
	class type_hash : public type_interface
	{
		std::unordered_map<std::string, std::string> value;
	public:
		type_hash(const timeval_type & current);
		virtual ~type_hash();
		virtual std::string get_type();
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
