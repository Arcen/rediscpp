#include "server.h"
#include "client.h"
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
		multi_executing = true;
		transaction = false;
		return true;
	}
	bool client_type::in_exec() const
	{
		return multi_executing;
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
		auto & watching = client->get_watching();
		auto current = client->get_time();
		//全体ロックを行う
		std::map<int,std::shared_ptr<database_write_locker>> dbs;
		for (int i = 0; i < databases.size(); ++i) {
			dbs[i].reset(new database_write_locker(writable_db(i, client)));
		}
		for (auto it = watching.begin(), end = watching.end(); it != end; ++it) {
			auto & watch = *it;
			auto key = std::get<0>(watch);
			auto index = std::get<1>(watch);
			auto & db = *dbs[index];
			auto value = db->get(key, current);
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
			if (!client->execute()) {
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
		multi_executing = false;
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
			client->watch(arguments[i]);
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
