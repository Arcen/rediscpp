#include "server.h"
#include "log.h"

namespace rediscpp
{
	string_type::string_type(const std::string & string_value_, const timeval_type & current)
		: value_interface(current)
		, string_value(string_value_)
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
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto db = readable_db(client);
		auto & key = arguments[1];
		auto current = client->get_time();
		auto value = db->get(key, current);
		if (!value.get()) {
			client->response_null();
			return true;
		}
		std::shared_ptr<string_type> str = std::dynamic_pointer_cast<string_type>(value);
		if (!str.get()) {
			throw std::runtime_error("ERR type mismatch");
		}
		client->response_bulk(str->get());
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
		auto & arguments = client->get_arguments();
		auto size = arguments.size();
		if (size < 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & key = arguments[1];
		auto & value = arguments[2];
		int64_t expire = -1;//in millisec
		bool nx = false;
		bool xx = false;
		for (int i = 3; i < size; ++i) {
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
		return api_set_internal(client, key, value, nx, xx, expire);
	}
	///設定
	///@param[in] key キー名
	///@param[in] value 値
	///@note Available since 1.0.0.
	bool server_type::api_setnx(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & key = arguments[1];
		auto & value = arguments[2];
		return api_set_internal(client, key, value, true, false, -1);
	}
	///設定
	///@param[in] key キー名
	///@param[in] seconds 期限を設定
	///@param[in] value 値
	///@note Available since 2.0.0.
	bool server_type::api_setex(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 4) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & key = arguments[1];
		int64_t expire = atoi64(arguments[2]) * 1000;
		auto & value = arguments[3];
		return api_set_internal(client, key, value, false, false, expire);
	}
	///設定
	///@param[in] key キー名
	///@param[in] value 値
	///@note Available since 1.0.0.
	bool server_type::api_psetex(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 4) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & key = arguments[1];
		int64_t expire = atoi64(arguments[2]);
		auto & value = arguments[3];
		return api_set_internal(client, key, value, false, false, expire);
	}
	bool server_type::api_set_internal(client_type * client, const std::string & key, const std::string & value, bool nx, bool xx, int64_t expire)
	{
		auto db = writable_db(client);
		auto current = client->get_time();
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
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & key = arguments[1];
		auto db = readable_db(client);
		auto current = client->get_time();
		auto value = db->get(key, current);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		std::shared_ptr<string_type> str = std::dynamic_pointer_cast<string_type>(value);
		if (!str.get()) {
			throw std::runtime_error("ERR type mismatch");
		}
		client->response_integer(str->get().size());
		return true;
	}
	///追加
	///@param[in] key キー名
	///@note Available since 2.0.0.
	bool server_type::api_append(client_type * client)
	{
		auto & arguments = client->get_arguments();
		auto size = arguments.size();
		if (size < 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & key = arguments[1];
		auto & value = arguments[2];
		auto db = writable_db(client);
		auto current = client->get_time();
		auto now = db->get(key, current);
		if (now.get()) {
			std::shared_ptr<string_type> str = std::dynamic_pointer_cast<string_type>(now);
			if (!str.get()) {
				throw std::runtime_error("ERR type mismatch");
			}
			int64_t len = str->append(value);
			now->update(current);
			client->response_integer(len);
		} else {
			std::shared_ptr<string_type> str(new string_type(value, current));
			if (!db->insert(key, str, current)) {
				throw std::runtime_error("ERR internal error");
			}
			client->response_integer(value.size());
		}
		return true;
	}
	static int64_t str_pos_fix(int64_t pos, const std::string & str)
	{
		if (pos < 0) {
			pos = 1 - pos;
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
	bool server_type::api_getrange(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() < 4) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & key = arguments[1];
		auto current = client->get_time();
		auto db = readable_db(client);
		auto value = db->get(key, current);
		if (!value.get()) {
			client->response_null();
			return true;
		}
		std::shared_ptr<string_type> str = std::dynamic_pointer_cast<string_type>(value);
		if (!str.get()) {
			throw std::runtime_error("ERR type mismatch");
		}
		auto & strval = str->get();
		int64_t start = str_pos_fix(atoi64(arguments[2]), strval);
		int64_t end = str_pos_fix(atoi64(arguments[3]), strval);
		if (end <= start) {
			client->response_null();
		} else {
			client->response_bulk(strval.substr(start, end - start));
		}
		return true;
	}
	bool server_type::api_setrange(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() < 4) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & key = arguments[1];
		int64_t offset = atoi64(arguments[2]);
		if (offset < 0) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & newstr = arguments[3];
		auto current = client->get_time();
		auto db = writable_db(client);
		auto now = db->get(key, current);
		if (now.get()) {
			std::shared_ptr<string_type> str = std::dynamic_pointer_cast<string_type>(now);
			if (!str.get()) {
				throw std::runtime_error("ERR type mismatch");
			}
			int64_t len = str->setrange(offset, newstr);
			now->update(current);
			client->response_integer(len);
		} else {
			std::string newvalue;
			newvalue.reserve(offset + newstr.size());
			newvalue.resize(offset);
			newvalue.append(newstr);
			std::shared_ptr<string_type> str(new string_type(newvalue, current));
			if (!db->insert(key, str, current)) {
				throw std::runtime_error("ERR internal error");
			}
			client->response_integer(newvalue.size());
		}
		return true;
	}
	/*
	bool api_getset(client_type * client);
	bool api_mget(client_type * client);
	bool api_mset(client_type * client);
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
	*/
}
