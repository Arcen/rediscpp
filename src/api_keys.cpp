#include "server.h"
#include "client.h"
#include "type_string.h"
#include "type_hash.h"
#include "type_list.h"
#include "type_set.h"
#include "type_zset.h"

namespace rediscpp
{
	
	///キーをすべて列挙する
	///@note Available since 1.0.0.
	bool server_type::api_keys(client_type * client)
	{
		auto db = readable_db(client);
		auto & pattern = client->get_argument(1);
		std::unordered_set<std::string> keys;
		db->match(keys, pattern);
		client->response_start_multi_bulk(keys.size());
		for (auto it = keys.begin(), end = keys.end(); it != end; ++it) {
			client->response_bulk(*it);
		}
		return true;
	}
	///キーを削除する
	///@note Available since 1.0.0.
	bool server_type::api_del(client_type * client)
	{
		int64_t removed = 0;
		auto db = writable_db(client);
		auto current = client->get_time();
		auto & keys = client->get_keys();
		for (auto it = keys.begin(), end = keys.end(); it != end; ++it) {
			auto & key = **it;
			if (db->erase(key, current)) {
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
		auto db = readable_db(client);
		auto & keys = client->get_keys();
		auto current = client->get_time();
		for (auto it = keys.begin(), end = keys.end(); it != end; ++it) {
			auto & key = **it;
			if (db->get(key, current).get()) {
				client->response_integer1();
			} else {
				client->response_integer0();
			}
			return true;
		}
		return false;
	}
	///キーの有効期限設定
	///@note Available since 1.0.0.
	bool server_type::api_expire(client_type * client)
	{
		return api_expire_internal(client, true, true);
	}
	///キーの有効期限設定
	///@note Available since 1.0.0.
	bool server_type::api_expireat(client_type * client)
	{
		return api_expire_internal(client, true, false);
	}
	///キーの有効期限設定
	///@note Available since 2.6.0.
	bool server_type::api_pexpire(client_type * client)
	{
		return api_expire_internal(client, false, true);
	}
	///キーの有効期限設定
	///@note Available since 2.6.0.
	bool server_type::api_pexpireat(client_type * client)
	{
		return api_expire_internal(client, false, false);
	}
	bool server_type::api_expire_internal(client_type * client, bool sec, bool ts)
	{
		auto current = client->get_time();
		timeval_type tv(0,0);
		int64_t time = atoi64(client->get_argument(2));
		if (ts) {
			tv = client->get_time();
			if (sec) {
				tv.tv_sec += time;
			} else {
				tv.add_msec(time);
			}
		} else {
			if (sec) {
				tv.tv_sec = time;
			} else {//msec
				tv.tv_sec = time / 1000;
				tv.tv_usec = (time % 1000) * 1000;
			}
		}
		auto & key = client->get_argument(1);
		auto db = writable_db(client);
		auto value = db->get(key, current);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		value->expire(tv);
		db->regist_expiring_key(tv, key);
		client->response_integer1();
		return true;
	}
	///キーの有効期限消去
	///@note Available since 2.2.0.
	bool server_type::api_persist(client_type * client)
	{
		auto db = writable_db(client);
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto value = db->get(key, current);
		if (!value.get()) {
			client->response_integer0();
		} else {
			value->persist();
			client->response_integer1();
		}
		return true;
	}
	///キーの有効期限確認
	///@note Available since 1.0.0.
	bool server_type::api_ttl(client_type * client)
	{
		return api_ttl_internal(client, true);
	}
	///キーの有効期限確認
	///@note Available since 2.6.0.
	bool server_type::api_pttl(client_type * client)
	{
		return api_ttl_internal(client, false);
	}
	bool server_type::api_ttl_internal(client_type * client, bool sec)
	{
		auto db = readable_db(client);
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto value = db->get(key, current);
		if (!value.get()) {
			client->response_integer(-2);
			return true;
		}
		if (!value->is_expiring()) {
			client->response_integer(-1);
			return true;
		}
		timeval_type ttl = value->ttl(current);
		uint64_t result = ttl.tv_sec * 1000 + ttl.tv_usec / 1000;
		if (sec) {
			result /= 1000;
		}
		client->response_integer(result);
		return true;
	}
	///キーの移動
	///@note Available since 1.0.0.
	bool server_type::api_move(client_type * client)
	{
		int dst_index = atoi64(client->get_argument(2));
		if (dst_index == client->get_db_index()) {
			client->response_integer0();
			return 0;
		}
		int src_index = client->get_db_index();
		std::map<int,std::shared_ptr<database_write_locker>> dbs;
		auto min_index = std::min(src_index, dst_index);
		auto max_index = std::max(src_index, dst_index);
		dbs[min_index].reset(new database_write_locker(writable_db(min_index, client)));
		dbs[max_index].reset(new database_write_locker(writable_db(max_index, client)));
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & src_db = *dbs[src_index];
		auto & dst_db = *dbs[dst_index];
		auto value = src_db->get(key, current);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		if (dst_db->get(key, current).get()) {
			client->response_integer0();
			return true;
		}
		if (!dst_db->insert(key, value, current)) {
			client->response_integer0();
			return false;
		}
		src_db->erase(key, current);
		client->response_integer1();
		return true;
	}
	///ランダムなキーの取得
	///@note Available since 1.0.0.
	bool server_type::api_randomkey(client_type * client)
	{
		auto db = writable_db(client);
		auto current = client->get_time();
		auto value = db->randomkey(current);
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
		auto db = writable_db(client);
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto value = db->get(key, current);
		if (!value.get()) {
			throw std::runtime_error("ERR not exist");
		}
		auto newkey = client->get_argument(2);
		if (key == newkey) {
			throw std::runtime_error("ERR same key");
		}
		db->erase(key, current);
		db->replace(newkey, value);
		client->response_ok();
		return true;
	}
	///キー名の変更(上書き不可)
	///@note Available since 1.0.0.
	bool server_type::api_renamenx(client_type * client)
	{
		auto db = writable_db(client);
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto value = db->get(key, current);
		if (!value.get()) {
			throw std::runtime_error("ERR not exist");
		}
		auto & newkey = client->get_argument(2);
		if (key == newkey) {
			throw std::runtime_error("ERR same key");
		}
		auto dst_value = db->get(newkey, current);
		if (dst_value.get()) {
			client->response_integer0();
			return true;
		}
		if (!db->insert(newkey, value, current)) {
			throw std::runtime_error("ERR internal error");
		}
		db->erase(key, current);
		client->response_integer1();
		return true;
	}
	///型
	///@note Available since 1.0.0.
	bool server_type::api_type(client_type * client)
	{
		auto db = readable_db(client);
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto value = db->get(key, current);
		if (!value.get()) {
			client->response_status("none");
			return true;
		}
		client->response_status(value->get_type());
		return true;
	}
	template<typename T1, typename T2>
	struct compare_first
	{
		bool operator()(const std::pair<T1,T2> & lhs, const std::pair<T1,T2> & rhs)
		{
			return lhs.first < rhs.first;
		}
	};
	struct pattern_type
	{
		bool constant;
		bool has_field;
		std::string prefix;
		std::string suffix;
		std::string field;
		pattern_type()
			: constant(true)
			, has_field(false)
		{
		}
		pattern_type(const pattern_type & rhs)
			: constant(rhs.constant)
			, has_field(rhs.has_field)
			, prefix(rhs.prefix)
			, suffix(rhs.suffix)
			, field(rhs.field)
		{
		}
		pattern_type(const std::string & pattern)
			: constant(true)
			, has_field(false)
		{
			auto asterisk = pattern.find('*');
			if (asterisk != std::string::npos) {
				constant = false;
				prefix = pattern.substr(0, asterisk);
				suffix = pattern.substr(asterisk + 1);
				auto arrow = suffix.find("->");
				if (arrow != std::string::npos) {
					has_field = true;
					field = suffix.substr(arrow + 2);
					suffix = suffix.substr(0, arrow);
				}
			} else {
				prefix = pattern;
			}
		}
		std::string get_key(const std::string & key) const { return constant ? prefix : prefix + key + suffix; }
	};
	static void sort(database_write_locker & db, std::vector<std::string> & values, bool by, const std::string & by_pattern, bool numeric, timeval_type current)
	{
		pattern_type pattern(by_pattern);
		const bool nosort = by && pattern.constant;
		if (nosort) {
			return;
		}
		std::vector<std::pair<long double,std::string>> num_list;
		std::vector<std::pair<std::string,std::string>> alp_list;
		if (numeric) {
			num_list.reserve(values.size());
		} else {
			alp_list.reserve(values.size());
		}
		for (auto it = values.begin(), end = values.end(); it != end; ++it) {
			auto & value = *it;
			std::string comp_value;
			if (by) {
				auto by_key = pattern.get_key(value);
				auto by_value = db->get(by_key, current);
				if (!pattern.has_field) {
					auto by_value_str = std::dynamic_pointer_cast<type_string>(by_value);
					if (by_value_str) {
						comp_value = by_value_str->get();
					}
				} else {
					auto by_value_hash = std::dynamic_pointer_cast<type_hash>(by_value);
					if (by_value_hash) {
						auto pair = by_value_hash->hget(pattern.field);
						if (pair.second) {
							comp_value = pair.second;
						}
					}
				}
			} else {
				comp_value = value;
			}
			if (numeric) {
				bool is_valid;
				long double num = atold(comp_value, is_valid);
				if (isnan(num)) {
					num = 0;
				}
				num_list.push_back(std::make_pair(num, value));
			} else {
				alp_list.push_back(std::make_pair(comp_value, value));
			}
		}
		values.clear();
		if (numeric) {
			std::stable_sort(num_list.begin(), num_list.end(), compare_first<long double,std::string>());
			for (auto it = num_list.begin(), end = num_list.end(); it != end; ++it) {
				values.push_back(it->second);
			}
		} else {
			std::stable_sort(alp_list.begin(), alp_list.end(), compare_first<std::string,std::string>());
			for (auto it = alp_list.begin(), end = alp_list.end(); it != end; ++it) {
				values.push_back(it->second);
			}
		}
	}
	///ソート
	///@note Available since 1.0.0.
	bool server_type::api_sort(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto & arguments = client->get_arguments();
		size_t parsed = 2;
		bool by = false;
		std::string by_pattern;
		const size_t size = arguments.size();
		if (parsed < size) {
			std::string keyword = arguments[parsed];
			std::transform(keyword.begin(), keyword.end(), keyword.begin(), toupper);
			if (keyword == "BY") {
				++parsed;
				if (size <= parsed) {
					throw std::runtime_error("ERR not found BY pattern");
				}
				by_pattern = arguments[parsed];
				by = true;
				++parsed;
			}
		}
		bool limit = false;
		int64_t limit_offset = 0;
		int64_t limit_count = 0;
		bool is_valid = false;
		if (parsed < size) {
			std::string keyword = arguments[parsed];
			std::transform(keyword.begin(), keyword.end(), keyword.begin(), toupper);
			if (keyword == "LIMIT") {
				limit = true;
				++parsed;
				if (size < parsed + 2) {
					throw std::runtime_error("ERR syntax error, not found limit parameter");
				}
				limit_offset = atoi64(client->get_argument(parsed), is_valid);
				if (!is_valid || limit_offset < 0) {
					throw std::runtime_error("ERR limit offset is not valid integer");
				}
				++parsed;
				limit_count = atoi64(client->get_argument(parsed), is_valid);
				if (!is_valid || limit_count < 0) {
					throw std::runtime_error("ERR limit count is not valid integer");
				}
				++parsed;
			}
		}
		std::vector<pattern_type> get_pattern;
		while (parsed < size) {
			std::string keyword = arguments[parsed];
			std::transform(keyword.begin(), keyword.end(), keyword.begin(), toupper);
			if (keyword == "GET") {
				++parsed;
				if (size < parsed + 1) {
					throw std::runtime_error("ERR syntax error, not found get pattern");
				}
				std::string pattern = arguments[parsed];
				get_pattern.push_back(pattern_type(pattern));
				++parsed;
			} else {
				break;
			}
		}
		bool asc = true;
		if (parsed < size) {
			std::string keyword = arguments[parsed];
			std::transform(keyword.begin(), keyword.end(), keyword.begin(), toupper);
			if (keyword == "ASC") {
				++parsed;
			} else if (keyword == "DESC") {
				++parsed;
				asc = false;
			}
		}
		bool numeric = true;
		if (parsed < size) {
			std::string keyword = arguments[parsed];
			std::transform(keyword.begin(), keyword.end(), keyword.begin(), toupper);
			if (keyword == "ALPHA") {
				++parsed;
				numeric = false;
			}
		}
		bool store = false;
		std::string destination;
		if (parsed < size) {
			std::string keyword = arguments[parsed];
			std::transform(keyword.begin(), keyword.end(), keyword.begin(), toupper);
			if (keyword == "STORE") {
				++parsed;
				if (size <= parsed) {
					throw std::runtime_error("ERR not found STORE destination");
				}
				destination = arguments[parsed];
				store = true;
				++parsed;
			}
		}
		if (parsed != size) {
			throw std::runtime_error("ERR syntax error");
		}
		auto current = client->get_time();
		auto db = writable_db(client, !store);
		auto value = db->get(key, current);
		size_t values_size = 0;
		std::vector<std::string> values;
		if (value.get()) {
			std::shared_ptr<type_list> list = std::dynamic_pointer_cast<type_list>(value);
			std::shared_ptr<type_set> set = std::dynamic_pointer_cast<type_set>(value);
			std::shared_ptr<type_zset> zset = std::dynamic_pointer_cast<type_zset>(value);
			if (list) {
				auto range = list->get_range();
				values.reserve(list->size());
				values.insert(values.end(), range.first, range.second);
			} else if (set) {
				auto range = set->smembers();
				values.reserve(set->size());
				values.insert(values.end(), range.first, range.second);
			} else if (zset) {
				auto range = zset->zrange();
				values.reserve(zset->size());
				for (auto it = range.first, end = range.second; it != end; ++it) {
					values.push_back((*it)->member);
				}
			} else {
				throw std::runtime_error("ERR sort only list, set, zset");
			}
			sort(db, values, by, by_pattern, numeric, current);
		}
		std::list<bool> nulls;
		std::list<std::string> list_values;
		if (get_pattern.empty()) {
			get_pattern.push_back(pattern_type("#"));
		}

		if (!asc) {
			std::reverse(values.begin(), values.end());
		}
		if (limit) {
			if (limit_count) {
				if (limit_offset + limit_count < values.size()) {
					values.erase(values.begin() + (limit_offset + limit_count), values.end());
				}
			}
			if (limit_offset) {
				if (values.size() <= limit_offset) {
					values.clear();
				} else {
					values.erase(values.begin(), values.begin() + limit_offset);
				}
			}
		}
		for (auto it = values.begin(), end = values.end(); it != end; ++it) {
			auto & value = *it;
			for (auto it = get_pattern.begin(), end = get_pattern.end(); it != end; ++it) {
				auto & pattern = *it;
				auto key = pattern.get_key(value);
				if (key == "#") {
					list_values.push_back(value);
					nulls.push_back(false);
					continue;
				}
				auto val = db->get(key, current);
				if (!pattern.has_field) {
					auto strval = std::dynamic_pointer_cast<type_string>(val);
					if (strval) {
						list_values.push_back(strval->get());
						nulls.push_back(false);
						continue;
					}
				} else {
					auto hashval = std::dynamic_pointer_cast<type_hash>(val);
					if (hashval) {
						auto v = hashval->hget(pattern.field);
						if (v.second) {
							list_values.push_back(v.first);
							nulls.push_back(false);
							continue;
						}
					}
				}
				list_values.push_back(std::string());
				nulls.push_back(true);
			}
		}
		values.clear();
		std::shared_ptr<type_list> result(new type_list(std::move(list_values), current));
		if (store) {
			client->response_integer(result->size());
		} else {
			client->response_start_multi_bulk(result->size());
			auto range = result->get_range();
			if (nulls.empty()) {
				for (auto it = range.first; it != range.second; ++it) {
					client->response_bulk(*it);
				}
			} else {
				auto nit = nulls.begin();
				for (auto it = range.first; it != range.second; ++it, ++nit) {
					client->response_bulk(*it, !*nit);
				}
			}
		}
		return true;
	}
}
