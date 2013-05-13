#include "server.h"
#include "client.h"
#include "type_set.h"

namespace rediscpp
{
	///複数のメンバーを追加
	///@note Available since 1.0.0.
	bool server_type::api_sadd(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & members = client->get_members();
		auto db = writable_db(client);
		auto set = db->get_set_with_expire(key, current);
		bool created = false;
		if (!set.second) {
			set.second.reset(new type_set());
			created = true;
		}
		int64_t added = set.second->sadd(members);
		if (created) {
			db->replace(key, expire_info(current), set.second);
		} else {
			set.first->update(current);
		}
		client->response_integer(added);
		return true;
	}
	///メンバーの数を取得
	///@note Available since 1.0.0.
	bool server_type::api_scard(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		std::shared_ptr<type_set> set = db->get_set(key, current);
		if (!set) {
			client->response_integer0();
			return true;
		}
		int64_t size = set->scard();
		client->response_integer(size);
		return true;
	}
	///メンバー確認
	///@note Available since 1.0.0.
	bool server_type::api_sismember(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & member = *client->get_members()[0];
		auto db = readable_db(client);
		std::shared_ptr<type_set> set = db->get_set(key, current);
		if (set && set->sismember(member)) {
			client->response_integer1();
		} else {
			client->response_integer0();
		}
		return true;
	}
	///全てのメンバーを取得
	///@note Available since 1.0.0.
	bool server_type::api_smembers(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		std::shared_ptr<type_set> set = db->get_set(key, current);
		if (!set) {
			client->response_null();
			return true;
		}
		auto range = set->smembers();
		client->response_start_multi_bulk(set->size());
		for (auto it = range.first, end = range.second; it != end; ++it) {
			client->response_bulk(*it);
		}
		return true;
	}
	///メンバーを移動
	///@note Available since 1.0.0.
	bool server_type::api_smove(client_type * client)
	{
		auto & srckey = client->get_argument(1);
		auto & destkey = client->get_argument(2);
		auto & member = *client->get_members()[0];
		auto current = client->get_time();
		auto db = writable_db(client);
		auto srcset = db->get_set_with_expire(srckey, current);
		auto dstset = db->get_set_with_expire(destkey, current);
		if (!srcset.second) {
			client->response_integer0();
			return true;
		}
		if (srckey == destkey) {
			srcset.first->update(current);
			client->response_integer1();
			return true;
		}
		if (!srcset.second->erase(member)) {
			client->response_integer0();
			return true;
		}
		bool inserted = false;
		if (!dstset.second) {
			dstset.second.reset(new type_set());
			inserted = dstset.second->insert(member);
			db->replace(destkey, expire_info(current), dstset.second);
		} else {
			inserted = dstset.second->insert(member);
			if (inserted) {
				dstset.first->update(current);
			}
		}
		if (srcset.second->empty()) {
			db->erase(srckey, current);
		} else {
			srcset.first->update(current);
		}
		if (inserted) {
			client->response_integer1();
		} else {
			client->response_integer0();
		}
		return true;
	}
	///メンバーをランダムに取り出し
	///@note Available since 1.0.0.
	bool server_type::api_spop(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = writable_db(client);
		auto set = db->get_set_with_expire(key, current);
		if (!set.second || set.second->empty()) {
			client->response_null();
			return true;
		}
		std::vector<std::set<std::string>::const_iterator> randmember;
		set.second->srandmember(1, randmember);
		std::string member = *randmember[0];
		set.second->erase(member);
		if (set.second->empty()) {
			db->erase(key, current);
		} else {
			set.first->update(current);
		}
		client->response_bulk(member);
		return true;
	}
	///メンバーをランダムに取得
	///@note Available since 1.0.0.
	bool server_type::api_srandmember(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto & argumens = client->get_arguments();
		auto current = client->get_time();
		auto db = readable_db(client);
		bool is_valid = true;
		int64_t count = atoi64(3 <= argumens.size() ? client->get_argument(2) : "0", is_valid);
		if (!is_valid) {
			throw std::runtime_error("ERR count is not valid integer");
		}
		std::shared_ptr<type_set> set = db->get_set(key, current);
		if (!set || set->empty()) {
			client->response_null();
			return true;
		}
		std::vector<std::set<std::string>::const_iterator> randmembers;
		if (0 < count) {
			set->srandmember_distinct(count, randmembers);
		} else {
			set->srandmember(-count, randmembers);
		}
		client->response_start_multi_bulk(randmembers.size());
		for (auto it = randmembers.begin(), end = randmembers.end(); it != end; ++it) {
			client->response_bulk(**it);
		}
		return true;
	}
	///メンバーを削除
	///@note Available since 1.0.0.
	bool server_type::api_srem(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto & argumens = client->get_arguments();
		auto current = client->get_time();
		auto & members = client->get_members();
		auto db = writable_db(client);
		auto set = db->get_set_with_expire(key, current);
		if (!set.second || set.second->empty()) {
			client->response_integer0();
			return true;
		}
		size_t removed = set.second->srem(members);
		if (set.second->empty()) {
			db->erase(key, current);
		} else {
			set.first->update(current);
		}
		client->response_integer(removed);
		return true;
	}
	bool server_type::api_sdiff(client_type * client)
	{
		return api_soperaion_internal(client, -1, false);
	}
	bool server_type::api_sdiffstore(client_type * client)
	{
		return api_soperaion_internal(client, -1, true);
	}
	bool server_type::api_sunion(client_type * client)
	{
		return api_soperaion_internal(client, 1, false);
	}
	bool server_type::api_sunionstore(client_type * client)
	{
		return api_soperaion_internal(client, 1, true);
	}
	bool server_type::api_sinter(client_type * client)
	{
		return api_soperaion_internal(client, 0, false);
	}
	bool server_type::api_sinterstore(client_type * client)
	{
		return api_soperaion_internal(client, 0, true);
	}
	///集合演算
	///@note Available since 1.0.0.
	///@param[in] type -1 : diff, 0 : inter, 1 : union
	///@param[in] store 保存するかどうか
	bool server_type::api_soperaion_internal(client_type * client, int type, bool store)
	{
		auto & keys = client->get_keys();
		auto & argumens = client->get_arguments();
		auto current = client->get_time();
		auto db = writable_db(client);
		std::shared_ptr<type_set> result(new type_set());
		auto it = keys.begin();
		auto end = keys.end();
		std::string destination;
		if (store) {
			destination = **it;
			++it;
			if (it == end) {
				throw std::runtime_error("ERR only destination");
			}
		}
		{
			std::shared_ptr<type_set> set = db->get_set(**it, current);
			if (set) {
				result->sunion(*set);
			}
			++it;
		}
		if (type < 0) {
			for (; it != end; ++it) {
				std::shared_ptr<type_set> set = db->get_set(**it, current);
				if (set) {
					result->sdiff(*set);
				}
			}
		} else if (0 < type) {
			for (; it != end; ++it) {
				std::shared_ptr<type_set> set = db->get_set(**it, current);
				if (set) {
					result->sunion(*set);
				}
			}
		} else {
			for (; it != end; ++it) {
				std::shared_ptr<type_set> set = db->get_set(**it, current);
				if (set) {
					result->sinter(*set);
				}
			}
		}
		if (store) {
			if (result->empty()) {
				db->erase(destination, current);
			} else {
				db->replace(destination, expire_info(current), result);
			}
			client->response_integer(result->size());
			return true;
		}
		if (result->empty()) {
			client->response_null();
			return true;
		}
		auto range = result->smembers();
		client->response_start_multi_bulk(result->size());
		for (auto it = range.first, end = range.second; it != end; ++it) {
			client->response_bulk(*it);
		}
		return true;
	}
};
