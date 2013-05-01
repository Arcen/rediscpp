#include "server.h"
#include "log.h"

namespace rediscpp
{
	///キーを削除する
	///@note Available since 1.0.0.
	bool server_type::api_del(client_type * client)
	{
		auto & arguments = client->get_arguments();
		int64_t removed = 0;
		auto & db = databases[client->get_db_index()];
		auto current = client->get_time();
		for (int i = 1, n = arguments.size(); i < n; ++i) {
			auto & key = arguments[i];
			if (key.second == false) {
				continue;
			}
			if (db.erase(key.first, current)) {
				++removed;
			}
		}
		client->response_integer(removed);
		return true;
	}
	///キーの存在確認
	///@note Available since 1.0.0.
	bool server_type::api_exists(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto current = client->get_time();
		if (key.second == false) {
			client->response_integer0();
			return true;
		}
		if (db.get(key.first, current).get()) {
			client->response_integer1();
		} else {
			client->response_integer0();
		}
		return true;
	}
	bool value_interface::is_expired(const timeval_type & current)
	{
		return expiring && expire_time <= current;
	}
	void value_interface::expire(const timeval_type & at)
	{
		expiring = true;
		expire_time = at;
	}
	void value_interface::persist()
	{
		expiring = false;
	}
	///キーの有効期限設定
	///@note Available since 1.0.0.
	bool server_type::api_expire(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto current = client->get_time();
		auto value = db.get(key, current);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		timeval_type tv = client->get_time();
		tv.tv_sec += strtoll(arguments[2].first.c_str(), NULL, 10);
		value->expire(tv);
		client->response_integer1();
		//@todo expireするキーのリストを作っておき、それを過ぎたら消すようにしたい
		return true;
	}
	///キーの有効期限設定
	///@note Available since 1.0.0.
	bool server_type::api_expireat(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto current = client->get_time();
		auto value = db.get(key, current);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		timeval_type tv(strtoll(arguments[2].first.c_str(), NULL, 10), 0);
		value->expire(tv);
		client->response_integer1();
		//@todo expireするキーのリストを作っておき、それを過ぎたら消すようにしたい
		return true;
	}
	///キーの有効期限設定
	///@note Available since 2.6.0.
	bool server_type::api_pexpire(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto current = client->get_time();
		auto value = db.get(key, current);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		timeval_type tv = client->get_time();
		tv.add_msec(strtoll(arguments[2].first.c_str(), NULL, 10));
		value->expire(tv);
		client->response_integer1();
		//@todo expireするキーのリストを作っておき、それを過ぎたら消すようにしたい
		return true;
	}
	///キーの有効期限設定
	///@note Available since 2.6.0.
	bool server_type::api_pexpireat(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto current = client->get_time();
		auto value = db.get(key, current);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		int64_t msec = strtoll(arguments[2].first.c_str(), NULL, 10);
		timeval_type tv(msec / 1000, (msec % 1000) * 1000);
		value->expire(tv);
		client->response_integer1();
		//@todo expireするキーのリストを作っておき、それを過ぎたら消すようにしたい
		return true;
	}
	///キーの有効期限消去
	///@note Available since 2.2.0.
	bool server_type::api_persist(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto current = client->get_time();
		auto value = db.get(key, current);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		value->persist();
		client->response_integer1();
		return true;
	}
	///キーの有効期限確認
	///@note Available since 1.0.0.
	bool server_type::api_ttl(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto current = client->get_time();
		auto value = db.get(key, current);
		if (!value.get()) {
			client->response_integer(-2);
			return true;
		}
		if (!value->is_expiring()) {
			client->response_integer(-1);
			return true;
		}
		timeval_type ttl = value->ttl(current);
		client->response_integer((ttl.tv_sec * 2 + ttl.tv_sec / 500000 + 1) / 2);
		return true;
	}
	///キーの有効期限確認
	///@note Available since 2.6.0.
	bool server_type::api_pttl(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto current = client->get_time();
		auto value = db.get(key, current);
		if (!value.get()) {
			client->response_integer(-2);
			return true;
		}
		if (!value->is_expiring()) {
			client->response_integer(-1);
			return true;
		}
		timeval_type ttl = value->ttl(current);
		client->response_integer(ttl.tv_sec * 1000 + ttl.tv_usec / 1000);
		return true;
	}
	///キーの移動
	///@note Available since 1.0.0.
	bool server_type::api_move(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto current = client->get_time();
		auto value = db.get(key, current);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		int dst_index = atoi(arguments[2].first.c_str());
		if (dst_index == client->get_db_index()) {
			client->response_integer0();
			return 0;
		}
		auto & dst_db = databases[dst_index];
		if (dst_db.get(key, current).get()) {
			client->response_integer0();
			return true;
		}
		if (!dst_db.insert(key.first, value)) {
			client->response_integer0();
			return false;
		}
		db.erase(key.first, current);
		client->response_integer1();
		return true;
	}
	///ランダムなキーの取得
	///@note Available since 1.0.0.
	bool server_type::api_randomkey(client_type * client)
	{
		auto & db = databases[client->get_db_index()];
		auto current = client->get_time();
		auto value = db.randomkey(current);
		if (value.empty()) {
			client->response_null();
		} else {
			client->response_bulk(value);
		}
		return true;
	}
	///キー名の変更
	///@note Available since 1.0.0.
	bool server_type::api_rename(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto current = client->get_time();
		auto value = db.get(key, current);
		if (!value.get()) {
			throw std::runtime_error("ERR not exist");
		}
		auto newkey = arguments[2].first;
		if (key.first == newkey) {
			throw std::runtime_error("ERR same key");
		}
		db.erase(key.first, current);
		db.insert(newkey, value);
		client->response_ok();
		return true;
	}
	///キー名の変更(上書き不可)
	///@note Available since 1.0.0.
	bool server_type::api_renamenx(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto current = client->get_time();
		auto value = db.get(key, current);
		if (!value.get()) {
			throw std::runtime_error("ERR not exist");
		}
		auto newkey = arguments[2];
		if (key.first == newkey.first) {
			throw std::runtime_error("ERR same key");
		}
		auto dst_value = db.get(newkey, current);
		if (dst_value.get()) {
			client->response_integer0();
			return true;
		}
		db.erase(key.first, current);
		db.insert(newkey.first, value);
		client->response_integer1();
		return true;
	}
	///型
	///@note Available since 1.0.0.
	bool server_type::api_type(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto current = client->get_time();
		auto value = db.get(key, current);
		if (!value.get()) {
			client->response_status("none");
			return true;
		}
		client->response_status(value->get_type());
		return true;
	}
}
