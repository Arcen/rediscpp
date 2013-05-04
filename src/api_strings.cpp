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
		int64_t start = pos_fix(atoi64(client->get_argument(2)), string.size());
		int64_t end = std::min<int64_t>(string.size(), pos_fix(atoi64(client->get_argument(3)), string.size()) + 1);
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
			auto value = std::dynamic_pointer_cast<string_type>(db->get(key, current));
			if (!value) {
				client->response_null();
			} else {
				client->response_bulk(value->get());
			}
		}
	}
	///複数の値を設定する
	///@param[in] key キー名
	///@param[in] value 値
	///@note Available since 1.0.1.
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
	///複数の値がすべて存在しない場合に設定する
	///@param[in] key キー名
	///@param[in] value 値
	///@note 他の型の値があると設定しない
	///@note Available since 1.0.1.
	bool server_type::api_msetnx(client_type * client)
	{
		auto db = writable_db(client);
		auto & keys = client->get_keys();
		auto & values = client->get_values();
		auto current = client->get_time();
		for (auto it = keys.begin(), end = keys.end(); it != end; ++it) {
			auto & key = **it;
			if (db->get(key, current)) {
				client->response_integer0();
				return true;
			}
		}
		for (auto kit = keys.begin(), kend = keys.end(), vit = values.begin(), vend = values.end(); kit != kend && vit != vend; ++kit, ++vit) {
			auto & key = **kit;
			auto & value = **vit;
			std::shared_ptr<string_type> str(new string_type(value, current));
			db->replace(key, str);
		}
		client->response_integer1();
		return true;
	}
	///1減らす
	///@param[in] key キー名
	///@param[in] value 値
	///@return 演算結果
	///@note 文字列型でないか、int64_tの範囲内でないか、演算結果がオーバーフローする場合にはエラーを返す
	///@note Available since 1.0.0.
	bool server_type::api_decr(client_type * client)
	{
		return api_incrdecr_internal(client, -1);
	}
	///1増やす
	///@param[in] key キー名
	///@param[in] value 値
	///@return 演算結果
	///@note 文字列型でないか、int64_tの範囲内でないか、演算結果がオーバーフローする場合にはエラーを返す
	///@note Available since 1.0.0.
	bool server_type::api_incr(client_type * client)
	{
		return api_incrdecr_internal(client, 1);
	}
	///指定量減らす
	///@param[in] key キー名
	///@param[in] value 値
	///@param[in] decrement 減らす量
	///@return 演算結果
	///@note 文字列型でないか、int64_tの範囲内でないか、演算結果がオーバーフローする場合にはエラーを返す
	///@note 最小値を減らそうとすると、オーバーフローするのでエラーとする
	///@note Available since 1.0.0.
	bool server_type::api_decrby(client_type * client)
	{
		auto & count = client->get_argument(2);
		bool is_valid = true;
		int64_t intval = atoi64(count, is_valid);
		if (!is_valid) {
			throw std::runtime_error("ERR decrement is not valid integer");
		}
		if (intval == std::numeric_limits<int64_t>::min()) {
			throw std::runtime_error("ERR decrement is out of range");
		}
		return api_incrdecr_internal(client, -intval);
	}
	///指定量増やす
	///@param[in] key キー名
	///@param[in] value 値
	///@param[in] increment 増やす量
	///@return 演算結果
	///@note 文字列型でないか、int64_tの範囲内でないか、演算結果がオーバーフローする場合にはエラーを返す
	///@note Available since 1.0.0.
	bool server_type::api_incrby(client_type * client)
	{
		auto & count = client->get_argument(2);
		bool is_valid = true;
		int64_t intval = atoi64(count, is_valid);
		if (!is_valid) {
			throw std::runtime_error("ERR increment is not valid integer");
		}
		return api_incrdecr_internal(client, intval);
	}
	int64_t server_type::incrby(const std::string & value, int64_t count)
	{
		bool is_valid;
		int64_t newval = count;
		int64_t oldval = atoi64(value, is_valid);
		if (!is_valid) {
			throw std::runtime_error("ERR not valid integer");
		}
		if (count < 0) {
			if (oldval < oldval + count) {
				throw std::runtime_error("ERR underflow");
			}
		} else if (0 < count) {
			if (oldval + count < oldval) {
				throw std::runtime_error("ERR overflow");
			}
		}
		return newval + oldval;
	}
	std::string server_type::incrbyfloat(const std::string & value, const std::string & increment)
	{
		bool is_valid;
		long double count = atold(increment, is_valid);
		if (!is_valid) {
			throw std::runtime_error("ERR increment is not valid float");
		}
		long double newval = count;
		long double oldval = atold(value, is_valid);
		if (!is_valid) {
			throw std::runtime_error("ERR not valid float");
		}
		newval = count + oldval;
		if (isnanl(newval) || isinfl(newval)) {
			throw std::runtime_error("ERR result is not finite");
		}
		return format("%Lg", newval);
	}
	///加減算を実行する
	bool server_type::api_incrdecr_internal(client_type * client, int64_t count)
	{
		auto db = writable_db(client);
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		bool is_valid = true;
		auto value = db->get_string(key, current);
		int64_t newval = incrby(value ? value->get() : "0", count);
		std::shared_ptr<string_type> str(new string_type(format("%"PRId64, newval), current));
		db->replace(key, str);
		client->response_integer(newval);
		return true;
	}
	///浮動小数点演算で加算する
	///@param[in] key キー名
	///@param[in] value 値
	///@param[in] increment 増やす量
	///@return 演算結果
	///@note 文字列型でないか、long doubleの範囲内でないか、演算結果が非有限・非数になる場合にはエラーを返す
	///@note Available since 1.0.0.
	bool server_type::api_incrbyfloat(client_type * client)
	{
		auto db = writable_db(client);
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto value = db->get_string(key, current);
		auto & increment = client->get_argument(2);
		std::string newstr = incrbyfloat(value ? value->get() : "0", increment);
		std::shared_ptr<string_type> str(new string_type(newstr, current));
		db->replace(key, str);
		client->response_bulk(newstr);
		return true;
	}
	///範囲内のビット数を計算する
	///@param[in] key キー名
	///@param[in] start 開始位置(省略可能)
	///@param[in] end 終了位置(省略可能)
	///@note Available since 2.6.0.
	bool server_type::api_bitcount(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		auto value = db->get_string(key, current);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		auto & string = value->get();
		auto & arguments = client->get_arguments();
		int64_t start = pos_fix(arguments.size() < 3 ? 0 : atoi64(client->get_argument(2)), string.size());
		int64_t end = std::min<int64_t>(string.size(), pos_fix(arguments.size() < 4 ? -1 : atoi64(client->get_argument(3)), string.size()) + 1);
		if (end <= start) {
			client->response_integer0();
		} else {
			int64_t count = 0;
			for (auto it = string.begin() + start, send = string.begin() + end; it != send; ++it) {
				count += bits_table[static_cast<uint8_t>(*it)];
			}
			client->response_integer(count);
		}
		return true;
	}
	///ビット演算を行って、指定のキーに出力する
	///@param[in] operation 演算
	///@param[in] destkey 出力先キー名
	///@param[in] key キー名
	///@note 応答は最大の値のサイズ。キーの最大サイズが0の場合には出力先は削除と同じ状態となる。
	///@note Available since 2.6.0.
	bool server_type::api_bitop(client_type * client)
	{
		auto operation = client->get_argument(1);
		std::transform(operation.begin(), operation.end(), operation.begin(), toupper);
		if (operation != "AND" && operation != "OR" && operation != "XOR" && operation != "NOT") {
			throw std::runtime_error("ERR operation is invalid");
		}
		auto & destkey = client->get_argument(2);
		auto & keys = client->get_keys();
		if (operation == "NOT" && keys.size() != 2) {
			throw std::runtime_error("ERR not operation require single key");
		}
		auto current = client->get_time();
		auto db = writable_db(client);
		std::vector<const std::string*> srcvalues;
		srcvalues.reserve(keys.size() - 1);
		size_t min_size = std::numeric_limits<size_t>::max();
		size_t max_size = 0;
		for (auto it = keys.begin() + 1, end = keys.end(); it != end; ++it) {
			auto & key = **it;
			auto srcvalue = db->get_string(key, current);
			if (srcvalue) {
				srcvalues.push_back(&srcvalue->ref());
				size_t size = srcvalue->get().size();
				min_size = std::min(min_size, size);
				max_size = std::max(max_size, size);
			} else {
				min_size = 0;
			}
		}
		std::string deststrval(max_size, '\0');
		if (operation == "NOT") {
			if (!srcvalues.empty()) {
				auto & src = **srcvalues.begin();
				auto dit = deststrval.begin();
				for (auto it = src.begin(), end = src.end(); it != end; ++it, ++dit) {
					*dit = ~ *it;
				}
			}
		} else {
			if (!srcvalues.empty()) {
				if (operation == "AND") {
					auto sit = srcvalues.begin();
					std::copy((**sit).begin(), (**sit).begin() + min_size, deststrval.begin());
					++sit;
					for (auto send = srcvalues.end(); sit != send; ++sit) {
						auto & src = **sit;
						auto dit = deststrval.begin();
						for (auto it = src.begin(), end = src.begin() + min_size; it != end; ++it, ++dit) {
							*dit &= *it;
						}
					}
				} else if (operation == "OR") {
					auto sit = srcvalues.begin();
					std::copy((**sit).begin(), (**sit).end(), deststrval.begin());
					++sit;
					for (auto send = srcvalues.end(); sit != send; ++sit) {
						auto & src = **sit;
						auto dit = deststrval.begin();
						for (auto it = src.begin(), end = src.end(); it != end; ++it, ++dit) {
							*dit |= *it;
						}
					}
				} else if (operation == "XOR") {
					auto sit = srcvalues.begin();
					std::copy((**sit).begin(), (**sit).end(), deststrval.begin());
					++sit;
					for (auto send = srcvalues.end(); sit != send; ++sit) {
						auto & src = **sit;
						auto dit = deststrval.begin();
						for (auto it = src.begin(), end = src.end(); it != end; ++it, ++dit) {
							*dit ^= *it;
						}
					}
				}
			}
		}
		if (max_size == 0) {
			db->erase(destkey, current);
		} else {
			std::shared_ptr<string_type> str(new string_type(deststrval, current));
			db->replace(destkey, str);
		}
		client->response_integer(max_size);
		return true;
	}
	///指定位置のビットを取得する
	///@param[in] key キー名
	///@param[in] offset 位置
	///@note Available since 2.2.0.
	bool server_type::api_getbit(client_type * client)
	{
		auto & key = client->get_argument(1);
		int64_t offset = atoi64(client->get_argument(2));
		if (offset < 0) {
			throw std::runtime_error("ERR offset is out of range");
		}
		auto current = client->get_time();
		auto db = readable_db(client);
		auto value = db->get_string(key, current);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		auto & string = value->get();
		int64_t offset_byte = offset / 8;
		if (string.size() <= offset_byte) {
			client->response_integer0();
			return true;
		}
		uint8_t target_value = static_cast<uint8_t>(string[offset_byte]);
		int64_t offset_bit = offset % 8;
		if (target_value & (0x80 >> offset_bit)) {
			client->response_integer1();
		} else {
			client->response_integer0();
		}
		return true;
	}
	///指定位置のビットを設定する
	///@param[in] key キー名
	///@param[in] offset 位置
	///@note 以前のビットを返す
	///@note Available since 2.2.0.
	bool server_type::api_setbit(client_type * client)
	{
		auto & key = client->get_argument(1);
		int64_t offset = atoi64(client->get_argument(2));
		if (offset < 0) {
			throw std::runtime_error("ERR offset is out of range");
		}
		int64_t set = atoi64(client->get_argument(3));
		if (set != 1 && set != 0) {
			throw std::runtime_error("ERR bit is invalid");
		}
		auto current = client->get_time();
		auto db = writable_db(client);
		auto value = db->get_string(key, current);
		int64_t offset_byte = offset / 8;
		int64_t offset_bit = offset % 8;
		if (!value.get()) {
			std::string string(offset_byte + 1, '\0');
			if (set) {
				*string.rbegin() = static_cast<char>(0x80 >> offset_bit);
			}
			std::shared_ptr<string_type> str(new string_type(string, current));
			db->replace(key, str);
			client->response_integer0();
			return true;
		}
		auto & string = value->ref();
		if (string.size() <= offset_byte) {
			string.resize(offset_byte + 1, '\0');
		}
		char target_bit = static_cast<char>(0x80 >> offset_bit);
		char & target_byte = string[offset_byte];
		char old = target_byte & target_bit;
		if (set) {
			target_byte |= target_bit;
		} else {
			target_byte &= ~target_bit;
		}
		value->update(current);
		if (old) {
			client->response_integer1();
		} else {
			client->response_integer0();
		}
		return true;
	}
}
