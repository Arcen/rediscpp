#include "server.h"
#include "client.h"
#include "master.h"
#include "log.h"

namespace rediscpp
{
	///データベースのキー数取得 
	///@note Available since 1.0.0.
	bool server_type::api_dbsize(client_type * client)
	{
		auto db = readable_db(client);
		client->response_integer(db->get_dbsize());
		return true;
	}
	///データベースの全キー消去 
	///@note Available since 1.0.0.
	bool server_type::api_flushall(client_type * client)
	{
		for (int i = 0, n = databases.size(); i < n; ++i) {
			auto db = writable_db(i, client);
			db->clear();
		}
		client->response_ok();
		return true;
	}
	///選択しているデータベースの全キー消去 
	///@note Available since 1.0.0.
	bool server_type::api_flushdb(client_type * client)
	{
		auto db = writable_db(client);
		db->clear();
		client->response_ok();
		return true;
	}
	///サーバのシャットダウン
	///@note NOSAVE, SAVEオプションは無視する
	///@note Available since 1.0.0.
	bool server_type::api_shutdown(client_type * client)
	{
		lputs(__FILE__, __LINE__, info_level, "shutdown start");
		shutdown = true;
		client->response_ok();
		return true;
	}
	///サーバの時間
	///@note Available since 2.6.0.
	///@note Time complexity: O(1)
	bool server_type::api_time(client_type * client)
	{
		timeval_type tv = client->get_time();
		client->response_start_multi_bulk(2);
		client->response_integer(tv.tv_sec);
		client->response_integer(tv.tv_usec);
		return true;
	}
	///スレーブ開始
	///@note Available since 1.0.0
	bool server_type::api_slaveof(client_type * client)
	{
		auto host = client->get_argument(1);
		auto port = client->get_argument(2);
		lprintf(__FILE__, __LINE__, debug_level, "SLAVEOF %s %s", host.c_str(), port.c_str());
		std::transform(host.begin(), host.end(), host.begin(), tolower);
		std::transform(port.begin(), port.end(), port.begin(), tolower);
		client->response_ok();
		client->flush();
		slaveof(host, port);
		return true;
	}
	///同期データ要求
	///@note Available since 1.0.0
	bool server_type::api_sync(client_type * client)
	{
		if (!save("/tmp/redis.save.rdb")) {
			throw std::runtime_error("ERR failed to sync");
		}
		client->response_file("/tmp/redis.save.rdb");
		client->set_slave();
		//@todo slaveとして、サーバに登録して、変更のあるコマンドをすべて転送する。但し、ファイルを送り終えるまでは、clientの送信バッファは使えない
		return true;
	}
	///クライアント情報設定
	///@note Available since 1.0.0
	bool server_type::api_replconf(client_type * client)
	{
		lputs(__FILE__, __LINE__, debug_level, "REPLCONF");
		auto & arguments = client->get_arguments();
		for (size_t i = 1, n = arguments.size(); i + 1 < n; i += 2) {
			auto field = arguments[i];
			auto & value = arguments[i+1];
			std::transform(field.begin(), field.end(), field.begin(), tolower);
			if (field == "listening-port") {
				bool is_valid;
				uint16_t port = atou16(value, is_valid);
				if (is_valid) {
					client->listening_port = port;
				}
			}
		}
		client->response_ok();
		return true;
	}
}
