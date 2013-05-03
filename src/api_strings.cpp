#include "server.h"
#include "log.h"

namespace rediscpp
{
	string_type::string_type(const std::string & string_value_, const timeval_type & current)
		: value_interface(current)
		, string_value(string_value_)
	{
	}
	string_type::string_type(std::string && string_value_, const timeval_type & current)
		: value_interface(current)
		, string_value(std::move(string_value_))
	{
	}
	const std::string & string_type::get()
	{
		return string_value;
	}
	///取得
	///@note Available since 1.0.0.
	bool server_type::api_get(client_type * client)
	{
		auto db = readable_db(client);
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto value = db->get_string(key, current);
		if (!value) {
			client->response_null();
			return true;
		}
		client->response_bulk(value->get());
		return true;
	}
	///設定
	///@param[in] key キー名
	///@param[in] value 値
	///@param[in] [EX seconds] 存在したら、期限を設定
	///@param[in] [PX milliseconds] 存在したら、ミリ秒で期限を設定
	///@param[in] [NX] 存在しない場合にのみ設定する
	///@param[in] [XX] 存在する場合にのみ設定する
	///@note Available since 1.0.0.
	bool server_type::api_set(client_type * client)
	{
		int64_t expire = -1;//in millisec
		bool nx = false;
		bool xx = false;
		auto & arguments = client->get_arguments();
		for (int i = 3, size = arguments.size(); i < size; ++i) {
			auto & option = arguments[i];
			if (option == "EX") {
				++i;
				if (i == size) {
					throw std::runtime_error("ERR syntax error");
				}
				auto & arg = arguments[i];
				expire = atoi64(arg) * 1000;
			} else if (option == "PX") {
				++i;
				if (i == size) {
					throw std::runtime_error("ERR syntax error");
				}
				auto & arg = arguments[i];
				expire = atoi64(arg);
			} else if (option == "NX") {
				if (xx) {
					throw std::runtime_error("ERR syntax error");
				}
				nx = true;
			} else if (option == "XX") {
				if (nx) {
					throw std::runtime_error("ERR syntax error");
				}
				xx = true;
			} else {
				throw std::runtime_error("ERR syntax error");
			}
		}
		return api_set_internal(client, nx, xx, expire);
	}
	///設定
	///@param[in] key キー名
	///@param[in] value 値
	///@note Available since 1.0.0.
	bool server_type::api_setnx(client_type * client)
	{
		return api_set_internal(client, true, false, -1);
	}
	///設定
	///@param[in] key キー名
	///@param[in] seconds 期限を設定
	///@param[in] value 値
	///@note Available since 2.0.0.
	bool server_type::api_setex(client_type * client)
	{
		int64_t expire = atoi64(client->get_argument(2)) * 1000;
		return api_set_internal(client, false, false, expire);
	}
	///設定
	///@param[in] key キー名
	///@param[in] value 値
	///@note Available since 1.0.0.
	bool server_type::api_psetex(client_type * client)
	{
		int64_t expire = atoi64(client->get_argument(2));
		return api_set_internal(client, false, false, expire);
	}
	bool server_type::api_set_internal(client_type * client, bool nx, bool xx, int64_t expire)
	{
		auto db = writable_db(client);
		auto current = client->get_time();
		auto & key = *client->get_keys()[0];
		auto & value = *client->get_values()[0];
		if (nx) {//存在を確認する
			if (db->get(key, current).get()) {
				client->response_null();
				return true;
			}
		} else if (xx) {
			if (!db->get(key, current).get()) {
				client->response_null();
				return true;
			}
		}
		std::shared_ptr<string_type> str(new string_type(value, current));
		if (0 <= expire) {
			timeval_type tv = current;
			tv.add_msec(expire);
			str->expire(tv);
			db->regist_expiring_key(tv, key);
		}
		db->replace(key, str);
		client->response_ok();
		return true;
	}
	///値の長さを取得
	///@param[in] key キー名
	///@note Available since 2.2.0.
	bool server_type::api_strlen(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto db = readable_db(client);
		auto current = client->get_time();
		auto value = db->get_string(key, current);
		if (!value) {
			client->response_integer0();
			return true;
		}
		client->response_integer(value->get().size());
		return true;
	}
	///追加
	///@param[in] key キー名
	///@note Available since 2.0.0.
	bool server_type::api_append(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto & value = client->get_argument(2);
		auto db = writable_db(client);
		auto current = client->get_time();
		auto now = db->get_string(key, current);
		if (now) {
			int64_t len = now->append(value);
			now->update(current);
			client->response_integer(len);
		} else {
			std::shared_ptr<string_type> str(new string_type(value, current));
			db->replace(key, str);
			client->response_integer(value.size());
		}
		return true;
	}
	static int64_t str_pos_fix(int64_t pos, const std::string & str)
	{
		if (pos < 0) {
			pos = - pos;
			if (str.size() < pos) {
				return 0;
			} else {
				return str.size() - pos;
			}
		} else {
			if (str.size() < pos) {
				return str.size();
			}
			return pos;
		}
	}
	///範囲内の値を取得する
	///@param[in] key キー名
	///@param[in] start 文字の開始位置
	///@param[in] end 文字の終了位置
	///@note オフセット位置がマイナスは終端からの位置となる
	///@note 範囲外は文字の範囲にクリップされる
	///@note 範囲は[start,end]となる
	///@note Available since 2.4.0.
	bool server_type::api_getrange(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		auto value = db->get_string(key, current);
		if (!value.get()) {
			client->response_null();
			return true;
		}
		auto & string = value->get();
		int64_t start = str_pos_fix(atoi64(client->get_argument(2)), string);
		int64_t end = std::min<int64_t>(string.size(), str_pos_fix(atoi64(client->get_argument(3)), string) + 1);
		if (end <= start) {
			client->response_null();
		} else {
			client->response_bulk(string.substr(start, end - start));
		}
		return true;
	}
	///値の一部を設定する
	///@param[in] key キー名
	///@param[in] offset オフセット位置
	///@param[in] value 設定する値
	///@note オフセット位置がマイナスはエラーとなる
	///@note オフセットが現在の文字長を超える場合は、NULLが挿入される
	///@note Available since 2.2.0.
	bool server_type::api_setrange(client_type * client)
	{
		auto & key = client->get_argument(1);
		int64_t offset = atoi64(client->get_argument(2));
		if (offset < 0) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & newstr = client->get_argument(3);
		auto current = client->get_time();
		auto db = writable_db(client);
		auto value = db->get_string(key, current);
		bool create = (!value);
		if (create) {
			value.reset(new string_type(std::string(), current));
		}
		int64_t len = value->setrange(offset, newstr);
		if (create) {
			db->replace(key, value);
		} else {
			value->update(current);
		}
		client->response_integer(len);
		return true;
	}
	///値を設定し、以前の値を取得する
	///@param[in] key キー名
	///@param[in] value 設定する値
	///@note Available since 1.0.0.
	bool server_type::api_getset(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto & newstr = client->get_argument(2);
		auto current = client->get_time();
		auto db = writable_db(client);
		auto value = db->get_string(key, current);
		bool create = (!value);
		if (create) {
			value.reset(new string_type(std::move(newstr), current));
			db->replace(key, value);
			client->response_null();
		} else {
			client->response_bulk(value->get());
			value->set(newstr);
			value->update(current);
		}
		return true;
	}
	///複数の値を取得する
	///@param[in] key キー名
	///@note 型が違ってもnullを返す
	///@note Available since 1.0.0.
	bool server_type::api_mget(client_type * client)
	{
		auto db = readable_db(client);
		auto & keys = client->get_keys();
		auto current = client->get_time();
		client->response_start_multi_bulk(keys.size());
		for (auto it = keys.begin(), end = keys.end(); it != end; ++it) {
			auto & key = **it;
			try {
				auto value = db->get_string(key, current);
				if (!value) {
					client->response_null();
				} else {
					client->response_bulk(value->get());
				}
			} catch (...) {
				client->response_null();
			}
		}
	}
	bool server_type::api_mset(client_type * client)
	{
		auto db = writable_db(client);
		auto & keys = client->get_keys();
		auto & values = client->get_values();
		auto current = client->get_time();
		for (auto kit = keys.begin(), kend = keys.end(), vit = values.begin(), vend = values.end(); kit != kend && vit != vend; ++kit, ++vit) {
			auto & key = **kit;
			auto & value = **vit;
			std::shared_ptr<string_type> str(new string_type(value, current));
			db->replace(key, str);
		}
		client->response_ok();
	}
	bool api_msetnx(client_type * client);
	bool api_decr(client_type * client);
	bool api_decrby(client_type * client);
	bool api_incr(client_type * client);
	bool api_incrby(client_type * client);
	bool api_incrbyfloat(client_type * client);
	bool api_bitcount(client_type * client);
	bool api_bitop(client_type * client);
	bool api_getbit(client_type * client);
	bool api_setbit(client_type * client);
}
