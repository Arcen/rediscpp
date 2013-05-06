#include "server.h"
#include "type_hash.h"

namespace rediscpp
{
	///複数のフィールドを削除
	///@note Available since 2.0.0.
	bool server_type::api_hdel(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = writable_db(client);
		auto & fields = client->get_keys();
		std::shared_ptr<hash_type> hash = db->get_hash(key, current);
		if (!hash) {
			client->response_integer0();
			return true;
		}
		int64_t removed = hash->hdel(fields);
		if (hash->empty()) {
			db->erase(key, current);
		} else {
			hash->update(current);
		}
		client->response_integer(removed);
		return true;
	}
	///フィールドの存在確認
	///@note Available since 2.0.0.
	bool server_type::api_hexists(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		auto & field = client->get_argument(2);
		std::shared_ptr<hash_type> hash = db->get_hash(key, current);
		if (!hash) {
			client->response_integer0();
			return true;
		}
		client->response_integer(hash->hexists(field) ? 1 : 0);
		return true;
	}
	///値の取得
	///@note Available since 2.0.0.
	bool server_type::api_hget(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		auto & field = client->get_argument(2);
		std::shared_ptr<hash_type> hash = db->get_hash(key, current);
		if (!hash) {
			client->response_null();
			return true;
		}
		auto r = hash->hget(field);
		if (!r.second) {
			client->response_null();
			return true;
		}
		client->response_bulk(r.first);
		return true;
	}
	///複数の値の取得
	///@note Available since 2.0.0.
	bool server_type::api_hmget(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		auto & fields = client->get_keys();
		std::shared_ptr<hash_type> hash = db->get_hash(key, current);
		if (!hash) {
			client->response_null();
			return true;
		}
		client->response_start_multi_bulk(fields.size());
		for (auto it = fields.begin(), end = fields.end(); it != end; ++it) {
			auto & field = **it;
			auto r = hash->hget(field);
			if (!r.second) {
				client->response_null();
			} else {
				client->response_bulk(r.first);
			}
		}
		return true;
	}
	///長さの取得
	///@note Available since 2.0.0.
	bool server_type::api_hlen(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		std::shared_ptr<hash_type> hash = db->get_hash(key, current);
		if (!hash) {
			client->response_integer0();
			return true;
		}
		client->response_integer(hash->size());
		return true;
	}
	///キーと値の全取得
	///@note Available since 2.0.0.
	bool server_type::api_hgetall(client_type * client)
	{
		return api_hgetall_internal(client, true, true);
	}
	///キーの全取得
	///@note Available since 2.0.0.
	bool server_type::api_hkeys(client_type * client)
	{
		return api_hgetall_internal(client, true, false);
	}
	///値の全取得
	///@note Available since 2.0.0.
	bool server_type::api_hvals(client_type * client)
	{
		return api_hgetall_internal(client, false, true);
	}
	bool server_type::api_hgetall_internal(client_type * client, bool keys, bool vals)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		auto & field = client->get_argument(2);
		std::shared_ptr<hash_type> hash = db->get_hash(key, current);
		if (!hash) {
			client->response_null();
			return true;
		}
		size_t size = hash->size();
		if (!size) {
			client->response_null();
			return true;
		}
		auto r = hash->hgetall();
		if (keys && vals) {
			client->response_start_multi_bulk(hash->size() * 2);
			for (auto it = r.first, end = r.second; it != end; ++it) {
				client->response_bulk(it->first);
				client->response_bulk(it->second);
			}
		} else {
			client->response_start_multi_bulk(hash->size());
			if (keys) {
				for (auto it = r.first, end = r.second; it != end; ++it) {
					client->response_bulk(it->first);
				}
			} else {
				for (auto it = r.first, end = r.second; it != end; ++it) {
					client->response_bulk(it->second);
				}
			}
		}
		return true;
	}
	///値の加算
	///@note Available since 2.0.0.
	bool server_type::api_hincrby(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & field = client->get_argument(2);
		auto & increment = client->get_argument(3);
		bool is_valid = true;
		int64_t intval = atoi64(increment, is_valid);
		if (!is_valid) {
			throw std::runtime_error("ERR increment is not valid integer");
		}
		auto db = writable_db(client);
		std::shared_ptr<hash_type> hash = db->get_hash(key, current);
		std::string oldval = "0";
		if (hash) {
			auto r = hash->hget(field);
			if (r.second) {
				oldval = r.first;
			}
		}
		int64_t newval = incrby(oldval, intval);
		std::string newstr = format("%"PRId64, newval);
		if (!hash) {
			hash.reset(new hash_type(current));
			hash->hset(field, newstr);
			db->replace(key, hash);
		} else {
			hash->hset(field, newstr);
			hash->update(current);
		}
		db->replace(key, hash);
		client->response_integer(newval);
		return true;
	}
	///値の加算
	///@note Available since 2.6.0.
	bool server_type::api_hincrbyfloat(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & field = client->get_argument(2);
		auto & increment = client->get_argument(3);
		auto db = writable_db(client);
		std::shared_ptr<hash_type> hash = db->get_hash(key, current);
		std::string oldval = "0";
		if (hash) {
			auto r = hash->hget(field);
			if (r.second) {
				oldval = r.first;
			}
		}
		std::string newstr = incrbyfloat(oldval, increment);
		if (!hash) {
			hash.reset(new hash_type(current));
			hash->hset(field, newstr);
			db->replace(key, hash);
		} else {
			hash->hset(field, newstr);
			hash->update(current);
		}
		db->replace(key, hash);
		client->response_bulk(newstr);
		return true;
	}
	///値の設定
	///@note Available since 2.0.0.
	bool server_type::api_hset(client_type * client)
	{
		return api_hset_internal(client, false);
	}
	///存在しなければ値の設定
	///@note Available since 2.0.0.
	bool server_type::api_hsetnx(client_type * client)
	{
		return api_hset_internal(client, true);
	}
	bool server_type::api_hset_internal(client_type * client, bool nx)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = writable_db(client);
		auto & field = client->get_argument(2);
		auto & value = client->get_argument(3);
		std::shared_ptr<hash_type> hash = db->get_hash(key, current);
		bool create = true;
		if (hash) {
			create = hash->hset(field, value, nx);
			if (create || !nx) {
				hash->update(current);
			}
		} else {
			hash.reset(new hash_type(current));
			hash->hset(field, value);
			db->replace(key, hash);
		}
		client->response_integer(create ? 1 : 0);
		return true;
	}
	bool server_type::api_hmset(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = writable_db(client);
		auto & fields = client->get_keys();
		auto & values = client->get_values();
		std::shared_ptr<hash_type> hash = db->get_hash(key, current);
		if (!hash) {
			hash.reset(new hash_type(current));
			db->replace(key, hash);
		}
		for (auto it = fields.begin(), end = fields.end(), vit = values.begin(), vend = values.end(); it != end && vit != vend; ++it, ++vit) {
			auto & field = **it;
			auto & value = **vit;
			hash->hset(field, value);
		}
		hash->update(current);
		client->response_ok();
		return true;
	}
};
