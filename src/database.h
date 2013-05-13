#ifndef INCLUDE_REDIS_CPP_DATABASE_H
#define INCLUDE_REDIS_CPP_DATABASE_H

#include "network.h"
#include "thread.h"
#include "type_interface.h"
#include "expire_info.h"

namespace rediscpp
{
	class client_type;
	class database_type;
	class database_write_locker
	{
		database_type * database;
		std::shared_ptr<rwlock_locker> locker;
	public:
		database_write_locker(database_type * database_, client_type * client, bool rdlock);
		database_type * get() { return database; }
		database_type * operator->() { return database; }
	};
	class database_read_locker
	{
		database_type * database;
		std::shared_ptr<rwlock_locker> locker;
	public:
		database_read_locker(database_type * database_, client_type * client);
		const database_type * get() { return database; }
		const database_type * operator->() { return database; }
	};
	class database_type
	{
		friend class database_write_locker;
		friend class database_read_locker;
		std::unordered_map<std::string,std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_interface>>> values;
		mutable mutex_type expire_mutex;
		mutable std::multimap<timeval_type,std::string> expires;
		rwlock_type rwlock;
		database_type(const database_type &);
	public:
		typedef std::unordered_map<std::string,std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_interface>>>::const_iterator const_iterator;
		database_type();
		size_t get_dbsize() const;
		void clear();
		std::shared_ptr<type_interface> get(const std::string & key, const timeval_type & current) const;
		std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_interface>> get_with_expire(const std::string & key, const timeval_type & current) const;
		std::shared_ptr<type_string> get_string(const std::string & key, const timeval_type & current) const;
		std::shared_ptr<type_list> get_list(const std::string & key, const timeval_type & current) const;
		std::shared_ptr<type_hash> get_hash(const std::string & key, const timeval_type & current) const;
		std::shared_ptr<type_set> get_set(const std::string & key, const timeval_type & current) const;
		std::shared_ptr<type_zset> get_zset(const std::string & key, const timeval_type & current) const;
		std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_string>> get_string_with_expire(const std::string & key, const timeval_type & current) const;
		std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_list>> get_list_with_expire(const std::string & key, const timeval_type & current) const;
		std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_hash>> get_hash_with_expire(const std::string & key, const timeval_type & current) const;
		std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_set>> get_set_with_expire(const std::string & key, const timeval_type & current) const;
		std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_zset>> get_zset_with_expire(const std::string & key, const timeval_type & current) const;
		bool erase(const std::string & key, const timeval_type & current);
		bool insert(const std::string & key, const expire_info & expire, std::shared_ptr<type_interface> value, const timeval_type & current);
		void replace(const std::string & key, const expire_info & expire, std::shared_ptr<type_interface> value);
		std::string randomkey(const timeval_type & current);
		void regist_expiring_key(timeval_type tv, const std::string & key) const;
		void flush_expiring_key(const timeval_type & current);
		void match(std::unordered_set<std::string> & result, const std::string & pattern) const;
		std::pair<const_iterator,const_iterator> range() const { return std::make_pair(values.begin(), values.end()); }
	};
};

#endif
