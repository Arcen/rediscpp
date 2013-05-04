#include "server.h"

namespace rediscpp
{
	///ブロックしつつ左側から取得
	///@note Available since 2.0.0.
	bool server_type::api_blpop(client_type * client)
	{
		auto & keys = client->get_keys();
		auto current = client->get_time();
		auto db = writable_db(client);
		bool in_blocking = client->is_blocked();
		for (auto it = keys.begin(), end = keys.end(); it != end; ++it) {
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
				const std::string & result = list->lpop();
				client->response_bulk(result);
				if (in_blocking) {//ブロック中に付解除
					client->end_blocked();
					unblocked(client->get());
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
	///左側へ追加
	///@note Available since 1.0.0.
	bool server_type::api_lpush(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & values = client->get_values();
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			list.reset(new list_type(current));
			list->lpush(values);
			db->replace(key, list);
		} else {
			list->lpush(values);
			list->update(current);
		}
		client->response_integer(list->size());
		if (client->in_exec()) {
			notify_list_pushed();
		} else {
			excecute_blocked_client();
		}
		return true;
	}
};
