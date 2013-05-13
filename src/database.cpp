#include "database.h"
#include "client.h"
#include "expire_info.h"
#include "type_hash.h"
#include "type_list.h"
#include "type_set.h"
#include "type_string.h"
#include "type_zset.h"

namespace rediscpp
{
	database_write_locker::database_write_locker(database_type * database_, client_type * client, bool rdlock)
		: database(database_)
		, locker(new rwlock_locker(database_->rwlock, client && client->in_exec() ? no_lock_type : (rdlock ? read_lock_type : write_lock_type)))
	{
	}
	database_read_locker::database_read_locker(database_type * database_, client_type * client)
		: database(database_)
		, locker(new rwlock_locker(database_->rwlock, client && client->in_exec() ? no_lock_type : read_lock_type))
	{
	}
	database_type::database_type()
	{
	}
	size_t database_type::get_dbsize() const
	{
		return values.size();
	}
	void database_type::clear()
	{
		values.clear();
	}
	std::shared_ptr<type_interface> database_type::get(const std::string & key, const timeval_type & current) const
	{
		auto it = values.find(key);
		if (it == values.end()) {
			return std::shared_ptr<type_interface>();
		}
		auto & value = it->second;
		if (value.first->is_expired(current)) {
			//values.erase(it);
			return std::shared_ptr<type_interface>();
		}
		return value.second;
	}
	std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_interface>> database_type::get_with_expire(const std::string & key, const timeval_type & current) const
	{
		auto it = values.find(key);
		if (it == values.end()) {
			return std::make_pair(std::shared_ptr<expire_info>(), std::shared_ptr<type_interface>());
		}
		auto & value = it->second;
		if (value.first->is_expired(current)) {
			//values.erase(it);
			return std::make_pair(std::shared_ptr<expire_info>(), std::shared_ptr<type_interface>());
		}
		return value;
	}
	template<typename T>
	std::shared_ptr<T> get_as(const database_type & db, const std::string & key, const timeval_type & current)
	{
		std::shared_ptr<type_interface> val = db.get(key, current);
		if (!val) {
			return std::shared_ptr<T>();
		}
		std::shared_ptr<T> value = std::dynamic_pointer_cast<T>(val);
		if (!value) {
			throw std::runtime_error("ERR type mismatch");
		}
		return value;
	}
	template<typename T>
	std::pair<std::shared_ptr<expire_info>,std::shared_ptr<T>> get_as_with_expire(const database_type & db, const std::string & key, const timeval_type & current)
	{
		std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_interface>> val = db.get_with_expire(key, current);
		if (!val.second) {
			return std::make_pair(std::shared_ptr<expire_info>(), std::shared_ptr<T>());
		}
		std::shared_ptr<T> value = std::dynamic_pointer_cast<T>(val.second);
		if (!value) {
			throw std::runtime_error("ERR type mismatch");
		}
		return std::make_pair(val.first, value);
	}
	std::shared_ptr<type_string> database_type::get_string(const std::string & key, const timeval_type & current) const { return get_as<type_string>(*this, key, current); }
	std::shared_ptr<type_list> database_type::get_list(const std::string & key, const timeval_type & current) const { return get_as<type_list>(*this, key, current); }
	std::shared_ptr<type_hash> database_type::get_hash(const std::string & key, const timeval_type & current) const { return get_as<type_hash>(*this, key, current); }
	std::shared_ptr<type_set> database_type::get_set(const std::string & key, const timeval_type & current) const { return get_as<type_set>(*this, key, current); }
	std::shared_ptr<type_zset> database_type::get_zset(const std::string & key, const timeval_type & current) const { return get_as<type_zset>(*this, key, current); }
	std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_string>> database_type::get_string_with_expire(const std::string & key, const timeval_type & current) const { return get_as_with_expire<type_string>(*this, key, current); }
	std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_list>> database_type::get_list_with_expire(const std::string & key, const timeval_type & current) const { return get_as_with_expire<type_list>(*this, key, current); }
	std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_hash>> database_type::get_hash_with_expire(const std::string & key, const timeval_type & current) const { return get_as_with_expire<type_hash>(*this, key, current); }
	std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_set>> database_type::get_set_with_expire(const std::string & key, const timeval_type & current) const { return get_as_with_expire<type_set>(*this, key, current); }
	std::pair<std::shared_ptr<expire_info>,std::shared_ptr<type_zset>> database_type::get_zset_with_expire(const std::string & key, const timeval_type & current) const { return get_as_with_expire<type_zset>(*this, key, current); }
	bool database_type::erase(const std::string & key, const timeval_type & current)
	{
		auto it = values.find(key);
		if (it == values.end()) {
			return false;
		}
		auto value = it->second;
		if (value.first->is_expired(current)) {
			values.erase(it);
			return false;
		}
		values.erase(it);
		return true;
	}
	bool database_type::insert(const std::string & key, const expire_info & expire, std::shared_ptr<type_interface> value, const timeval_type & current)
	{
		auto it = values.find(key);
		if (it == values.end()) {
			bool result = values.insert(std::make_pair(key, std::make_pair(std::shared_ptr<expire_info>(new expire_info(expire)), value))).second;
			if (result && expire.is_expiring()) {
				regist_expiring_key(expire.at(), key);
			}
			return result;
		} else {
			if (it->second.first->is_expired(current)) {
				it->second.first->set(expire);
				it->second.second = value;
				if (expire.is_expiring()) {
					regist_expiring_key(expire.at(), key);
				}
				return true;
			}
			return false;
		}
	}
	void database_type::replace(const std::string & key, const expire_info & expire, std::shared_ptr<type_interface> value)
	{
		auto & dst = values[key];
		dst.first.reset(new expire_info(expire));
		dst.second = value;
		if (expire.is_expiring()) {
			regist_expiring_key(expire.at(), key);
		}
	}
	std::string database_type::randomkey(const timeval_type & current)
	{
		while (!values.empty()) {
			auto it = values.begin();
			std::advance(it, rand() % values.size());
			if (it->second.first->is_expired(current)) {
				values.erase(it);
				continue;
			}
			return it->first;
		}
		return std::string();
	}
	void database_type::regist_expiring_key(timeval_type tv, const std::string & key) const
	{
		mutex_locker locker(expire_mutex);
		expires.insert(std::make_pair(tv, key));
	}
	void database_type::flush_expiring_key(const timeval_type & current)
	{
		mutex_locker locker(expire_mutex);
		if (expires.empty()) {
			return;
		}
		auto it = expires.begin(), end = expires.end();
		for (; it != end && it->first < current; ++it) {
			auto vit = values.find(it->second);
			if (vit != values.end() && vit->second.first->is_expired(current)) {
				values.erase(vit);
			}
		}
		expires.erase(expires.begin(), it);
	}
	void database_type::match(std::unordered_set<std::string> & result, const std::string & pattern) const
	{
		if (pattern == "*") {
			for (auto it = values.begin(), end = values.end(); it != end; ++it) {
				auto & key = it->first;
				result.insert(key);
			}
		} else {
			for (auto it = values.begin(), end = values.end(); it != end; ++it) {
				auto & key = it->first;
				if (pattern_match(pattern, key)) {
					result.insert(key);
				}
			}
		}
	}
};
