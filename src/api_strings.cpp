#include "server.h"
#include "log.h"

namespace rediscpp
{
	string_type::string_type(const argument_type & value)
		: int_value(0)
		, is_integer(false)
		, is_integer_and_string_mismatch(false)
	{
		if (!value.second) {
			return;
		}
		set_null(false);
		int_value = strtoll(value.first.c_str(), NULL, 10);
		string_value = format("%d", int_value);
		is_integer = (string_value == value.first);
		if (!is_integer) {
			string_value = value.first;
		}
	}
	const std::string & string_type::get()
	{
		if (is_integer_and_string_mismatch) {
			is_integer_and_string_mismatch = false;
			string_value = format("%d", int_value);
		}
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
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto value = db.get(key);
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
	//設定
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
		if (arguments.size() < 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & key = arguments[1];
		auto & value = arguments[2];
		int64_t expire = -1;//in millisec
		bool nx = false;
		bool xx = false;
		for (int i = 3; i < arguments.size(); ++i) {
			auto & option = arguments[i];
			if (option.first == "EX") {
				++i;
				if (i == arguments.size()) {
					throw std::runtime_error("ERR syntax error");
				}
				auto & arg = arguments[i];
				expire = strtoll(arg.first.c_str(), NULL, 10) * 1000;
			} else if (option.first == "PX") {
				++i;
				if (i == arguments.size()) {
					throw std::runtime_error("ERR syntax error");
				}
				auto & arg = arguments[i];
				expire = strtoll(arg.first.c_str(), NULL, 10);
			} else if (option.first == "NX") {
				if (xx) {
					throw std::runtime_error("ERR syntax error");
				}
				nx = true;
			} else if (option.first == "XX") {
				if (nx) {
					throw std::runtime_error("ERR syntax error");
				}
				xx = true;
			}
		}
		return api_set_internal(client, key, value, nx, xx, expire);
	}
	//設定
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
	//設定
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
		int64_t expire = strtoll(arguments[2].first.c_str(), NULL, 10) * 1000;
		auto & value = arguments[3];
		return api_set_internal(client, key, value, false, false, expire);
	}
	//設定
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
		int64_t expire = strtoll(arguments[2].first.c_str(), NULL, 10);
		auto & value = arguments[3];
		return api_set_internal(client, key, value, false, false, expire);
	}
	bool server_type::api_set_internal(client_type * client, const argument_type & key, const argument_type & value, bool nx, bool xx, int64_t expire)
	{
		auto & db = databases[client->get_db_index()];
		if (nx) {//存在を確認する
			if (db.get(key).get()) {
				client->response_null();
				return true;
			}
		} else if (xx) {
			if (!db.get(key).get()) {
				client->response_null();
				return true;
			}
		}
		db.erase(key.first);
		std::shared_ptr<string_type> str(new string_type(value));
		if (0 <= expire) {
			timeval_type tv;
			tv.add_msec(expire);
			str->expire(tv);
		}
		db.insert(key.first, str);
		client->response_ok();
		return true;
	}
	//設定
	///@param[in] key キー名
	///@note Available since 2.2.0.
	bool server_type::api_strlen(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & key = arguments[1];
		auto & db = databases[client->get_db_index()];
		auto value = db.get(key);
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

}
