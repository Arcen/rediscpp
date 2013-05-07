#include "server.h"
#include "client.h"
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
}