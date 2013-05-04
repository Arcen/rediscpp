#include "server.h"

namespace rediscpp
{
	///ブロックしつつ左側から取得
	///@note Available since 2.0.0.
	bool server_type::api_blpop(client_type * client)
	{
		return api_bpop_internal(client, true, false);
	}
	///ブロックしつつ右側から取得
	///@note Available since 2.0.0.
	bool server_type::api_brpop(client_type * client)
	{
		return api_bpop_internal(client, false, false);
	}
	///ブロックしつつ右側から取得し、左に追加
	///@note Available since 2.2.0.
	bool server_type::api_brpoplpush(client_type * client)
	{
		return api_bpop_internal(client, false, true);
	}
	///ブロックしつつ取得し、対象があれば左に追加する
	///@note 追加するとき、空かリストでなければ、取得せずにエラーを返す
	bool server_type::api_bpop_internal(client_type * client, bool left, bool rpoplpush)
	{
		auto & keys = client->get_keys();
		auto current = client->get_time();
		auto db = writable_db(client);
		bool in_blocking = client->is_blocked();
		auto kend = (rpoplpush ? keys.begin() : keys.end());
		if (rpoplpush) {
			++kend;
		}
		for (auto it = keys.begin(), end = kend; it != end; ++it) {
			auto & key = **it;
			if (in_blocking) {
				//ブロック中はリスト以外が設定されても待つ
				std::shared_ptr<value_interface> value = db->get(key, current);
				if (!value || !std::dynamic_pointer_cast<list_type>(value)) {
					continue;
				}
			}
			std::shared_ptr<list_type> list = db->get_list(key, current);
			if (!list) {
				continue;
			}
			if (!list->empty()) {
				bool created = false;
				try {
					if (rpoplpush) {
						auto &destkey = **keys.rbegin();
						std::shared_ptr<list_type> dest = db->get_list(destkey, current);
						const std::string & result = list->rpop();
						if (!dest) {
							created = true;
							dest.reset(new list_type(current));
							dest->lpush(result);
							db->replace(destkey, dest);
						} else {
							dest->lpush(result);
							dest->update(current);
						}
						client->response_bulk(result);
					} else {
						const std::string & result = left ? list->lpop() : list->rpop();
						client->response_bulk(result);
					}
					if (list->empty()) {
						db->erase(key, current);
					}
				} catch (std::exception e) {//rpoplpushで挿入先がリストで無い場合など
					client->response_error(e.what());
				}
				if (in_blocking) {//ブロック中に付解除
					client->end_blocked();
					unblocked(client->get());
				}
				//新規追加の場合は通知する
				if (created) {
					if (client->in_exec()) {
						notify_list_pushed();
					} else {
						excecute_blocked_client();
					}
				}
				return true;
			}
		}
		if (client->in_exec()) {//非ブロック
			client->response_null();
			return true;
		}
		if (in_blocking && !client->still_block()) {//タイムアウト
			client->end_blocked();
			client->response_null();
			return true;
		}
		if (!in_blocking) {//最初のブロックが起きた場合
			auto & arguments = client->get_arguments();
			int64_t timeout = atoi64(arguments.back());
			client->start_blocked(timeout);
			timer->insert(timeout, 0);//タイマーを追加
			//ブロックリストに追加
			blocked(client->get());
		}
		throw blocked_exception("blocking");
	}
	///取得し、対象があれば左に追加する
	///@note 追加するとき、空かリストでなければ、取得せずにエラーを返す
	bool server_type::api_rpoplpush(client_type * client)
	{
		auto current = client->get_time();
		auto db = writable_db(client);
		bool in_blocking = client->is_blocked();
		auto & key = client->get_argument(1);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list || list->empty()) {
			client->response_null();
			return true;
		}
		auto & destkey = client->get_argument(2);
		std::shared_ptr<list_type> dest = db->get_list(destkey, current);
		const std::string & result = list->rpop();
		bool created = false;
		if (!dest) {
			created = true;
			dest.reset(new list_type(current));
			dest->lpush(result);
			db->replace(destkey, dest);
		} else {
			dest->lpush(result);
			dest->update(current);
		}
		if (list->empty()) {
			db->erase(key, current);
		}
		client->response_bulk(result);
		//新規追加の場合は通知する
		if (created) {
			if (client->in_exec()) {
				notify_list_pushed();
			} else {
				excecute_blocked_client();
			}
		}
		return true;
	}
	///左側へ追加
	///@note Available since 1.0.0.
	bool server_type::api_lpush(client_type * client)
	{
		return api_lpush_internal(client, true, false);
	}
	///左側へリストがあれば追加
	///@note Available since 2.2.0.
	bool server_type::api_lpushx(client_type * client)
	{
		return api_lpush_internal(client, true, true);
	}
	///右側へ追加
	///@note Available since 1.0.0.
	bool server_type::api_rpush(client_type * client)
	{
		return api_lpush_internal(client, false, false);
	}
	///右側へリストがあれば追加
	///@note Available since 2.2.0.
	bool server_type::api_rpushx(client_type * client)
	{
		return api_lpush_internal(client, false, true);
	}
	///リストへ追加
	bool server_type::api_lpush_internal(client_type * client, bool left, bool exist)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & values = client->get_values();
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		bool created = false;
		if (!list) {
			if (exist) {
				client->response_integer0();
				return true;
			}
			list.reset(new list_type(current));
			if (left) {
				list->lpush(values);
			} else {
				list->rpush(values);
			}
			db->replace(key, list);
			created = true;
		} else {
			if (left) {
				list->lpush(values);
			} else {
				list->rpush(values);
			}
			list->update(current);
		}
		client->response_integer(list->size());
		//新規追加時の通知
		if (created) {
			if (client->in_exec()) {
				notify_list_pushed();
			} else {
				excecute_blocked_client();
			}
		}
		return true;
	}
	///リストの指定の値があれば、その前か後ろへ追加
	bool server_type::api_linsert(client_type * client)
	{
		auto side = client->get_argument(2);
		std::transform(side.begin(), side.end(), side.begin(), toupper);
		auto before = (side == "BEFORE");
		if (!before && side != "AFTER") {
			throw std::runtime_error("ERR syntax error");
		}

		auto & key = client->get_argument(1);
		auto & pivot = client->get_argument(3);
		auto & value = client->get_argument(4);
		auto current = client->get_time();
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list || !list->linsert(pivot, value, before)) {
			client->response_integer(0);
			return true;
		}
		list->update(current);
		client->response_integer(list->size());
		return true;
	}
	///左から取得
	///@note Available since 1.0.0.
	bool server_type::api_lpop(client_type * client)
	{
		return api_lpop_internal(client, true);
	}
	///右から取得
	///@note Available since 1.0.0.
	bool server_type::api_rpop(client_type * client)
	{
		return api_lpop_internal(client, false);
	}
	///リストから取得
	bool server_type::api_lpop_internal(client_type * client, bool left)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_null();
			return true;
		}
		const std::string & value = left ? list->lpop() : list->rpop();
		if (list->empty()) {
			db->erase(key, current);
		} else {
			list->update(current);
		}
		client->response_bulk(value);
		return true;
	}
	///リストの長さ
	///@note Available since 1.0.0.
	bool server_type::api_llen(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_integer(0);
			return true;
		}
		client->response_integer(list->size());
		return true;
	}
	///指定位置の値を取得
	///@note Available since 1.0.0.
	bool server_type::api_lindex(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_null();
			return true;
		}
		int64_t index = atoi64(client->get_argument(2));
		if (index < 0) {
			index += list->size();
		}
		if (index < 0 || list->size() <= index) {
			client->response_null();
			return true;
		}
		auto it = list->get_it(index);
		client->response_bulk(*it);
		return true;
	}
	///指定位置の範囲を取得
	///@note Available since 1.0.0.
	bool server_type::api_lrange(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_null_multi_bulk();
			return true;
		}
		int64_t start = pos_fix(atoi64(client->get_argument(2)), list->size());
		int64_t end = std::min<int64_t>(list->size(), pos_fix(atoi64(client->get_argument(3)), list->size()) + 1);
		if (end <= start) {
			client->response_null_multi_bulk();
			return true;
		}
		size_t count = end - start;
		auto range = list->get_range(start, end);
		client->response_start_multi_bulk(count);
		for (auto it = range.first, end = range.second; it != end; ++it) {
			client->response_bulk(*it);
		}
		return true;
	}
	///リストから指定の値を削除
	bool server_type::api_lrem(client_type * client)
	{
		auto & key = client->get_argument(1);
		int64_t count = atoi64(client->get_argument(2));
		auto & value = client->get_argument(3);
		auto current = client->get_time();
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_integer0();
			return true;
		}
		int64_t removed = list->lrem(count, value);
		if (list->empty()) {
			db->erase(key, current);
		} else {
			list->update(current);
		}
		client->response_integer(removed);
		return true;
	}
	///リストの有効範囲を指定して残りを削除する
	bool server_type::api_ltrim(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_ok();
			return true;
		}
		int64_t start = pos_fix(atoi64(client->get_argument(2)), list->size());
		int64_t end = std::min<int64_t>(list->size(), pos_fix(atoi64(client->get_argument(3)), list->size()) + 1);
		list->trim(start, end);
		if (list->empty()) {
			db->erase(key, current);
		} else {
			list->update(current);
		}
		client->response_ok();
		return true;
	}
	///リストの指定位置の値を変更する
	bool server_type::api_lset(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & value = client->get_argument(3);
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_ok();
			return true;
		}
		int64_t index = pos_fix(atoi64(client->get_argument(2)), list->size());
		if (!list->set(index, value)) {
			throw std::runtime_error("ERR index out of range");
		}
		list->update(current);
		client->response_ok();
		return true;
	}
};
