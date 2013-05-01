#include "server.h"
#include "log.h"

namespace rediscpp
{
	bool client_type::multi()
	{
		transaction = true;
		transaction_arguments.clear();
		return true;
	}
	bool client_type::exec()
	{
		transaction = false;
		return true;
	}
	bool client_type::queuing(const std::string & command)
	{
		if (!transaction) {
			return false;
		}
		if (command == "EXEC" || command == "DISCARD") {
			return false;
		}
		transaction_arguments.push_back(arguments);
		return true;
	}
	///トランザクションの開始
	///@note Available since 1.2.0.
	bool server_type::api_multi(client_type * client)
	{
		client->multi();
		client->response_ok();
		return true;
	}
	bool client_type::unqueue()
	{
		if (transaction_arguments.empty()) {
			return false;
		}
		transaction_arguments.front().swap(arguments);
		transaction_arguments.pop_front();
		return true;
	}
	///トランザクションの実行
	///@note Available since 1.2.0.
	bool server_type::api_exec(client_type * client)
	{
		//監視していた値が変更されていないか確認する
		//@todo マルチスレッドにするには確認しつつ、ロックを取得していく
		auto & watching = client->get_watching();
		auto current = client->get_time();
		for (auto it = watching.begin(), end = watching.end(); it != end; ++it) {
			auto & watch = *it;
			auto key = std::get<0>(watch);
			auto index = std::get<1>(watch);
			auto & db = databases[index];
			auto value = db.get(key, current);
			if (!value.get()) {
				client->response_null_multi_bulk();
				client->discard();
				return true;
			}
			auto watching_time = std::get<2>(watch);
			if (watching_time < value->get_last_modified_time()) {
				client->response_null_multi_bulk();
				client->discard();
				return true;
			}
		}
		auto count = client->get_transaction_size();
		client->exec();
		client->response_start_multi_bulk(count);
		for (auto i = 0; i < count; ++i) {
			client->unqueue();
			if (!execute(client)) {
				client->response_error("ERR unknown");
			}
		}
		client->discard();
		return true;
	}
	void client_type::discard()
	{
		transaction = false;
		transaction_arguments.clear();
		unwatch();
	}
	///トランザクションの中止
	///@note Available since 2.0.0.
	bool server_type::api_discard(client_type * client)
	{
		client->discard();
		client->response_ok();
		return true;
	}
	void client_type::watch(const std::string & key)
	{
		watching.insert(std::tuple<std::string,int,timeval_type>(key, db_index, timeval_type()));
	}
	///値の変更の監視
	///@note Available since 2.2.0.
	bool server_type::api_watch(client_type * client)
	{
		auto & arguments = client->get_arguments();
		for (int i = 1, n = arguments.size(); i < n; ++i) {
			client->watch(arguments[i].first);
		}
		client->response_ok();
		return true;
	}
	///値の変更の監視の中止
	///@note Available since 2.2.0.
	bool server_type::api_unwatch(client_type * client)
	{
		client->unwatch();
		client->response_ok();
		return true;
	}
}
