#include "server.h"
#include "log.h"
#include <algorithm>
#include <ctype.h>
#include <sys/time.h>

namespace rediscpp
{
	server_type::server_type()
		: shutdown(false)
	{
		databases.resize(1);
		build_function_map();
	}
	bool server_type::start(const std::string & hostname, const std::string & port)
	{
		std::shared_ptr<address_type> addr(new address_type);
		addr->set_hostname(hostname.c_str());
		addr->set_port(atoi(port.c_str()));
		listening = socket_type::create(*addr);
		listening->set_reuse();
		if (!listening->bind(addr)) {
			return false;
		}
		if (!listening->listen(100)) {
			return false;
		}
		listening->set_callback(server_event);
		listening->set_extra(this);
		listening->set_blocking(false);
		poll = poll_type::create(1000);
		poll->append(listening);
		while (true) {
			poll->wait(-1);
			if (shutdown) {
				if (poll->get_count() == 1) {
					lputs(__FILE__, __LINE__, info_level, "quit server, no client now");
					break;
				}
			}
		}
		return true;
	}
	void server_type::client_event(socket_type * s, int events)
	{
		if (!s) {
			return;
		}
		server_type * server = reinterpret_cast<server_type *>(s->get_extra());
		if (!server) {
			return;
		}
		server->on_client_event(s, events);
	}
	void server_type::server_event(socket_type * s, int events)
	{
		if (!s) {
			return;
		}
		server_type * server = reinterpret_cast<server_type *>(s->get_extra());
		if (!server) {
			return;
		}
		server->on_server_event(s, events);
	}
	void server_type::on_client_event(socket_type * s, int events)
	{
		if (events & EPOLLIN) {//recv
			//lputs(__FILE__, __LINE__, info_level, "client EPOLLIN");
			s->recv();
			while (s->should_recv()) {
				clients[s]->parse(this);
			}
			if (s->recv_done()) {
				auto sp = s->get();
				if (sp.get()) {
					lputs(__FILE__, __LINE__, info_level, "client closed");
					poll->remove(sp);
					clients.erase(s);
					return;
				}
			}
		}
		if (events & EPOLLOUT) {//send
			lputs(__FILE__, __LINE__, info_level, "client EPOLLOUT");
			s->send();
		}
		if (events & EPOLLRDHUP) {//相手側がrecvを行わなくなった
			lputs(__FILE__, __LINE__, info_level, "client EPOLLRDHUP");
		}
		if (events & EPOLLERR) {//相手にエラーが起こった
			lputs(__FILE__, __LINE__, info_level, "client EPOLLERR");
		}
		if (events & EPOLLHUP) {//ハングアップ
			lputs(__FILE__, __LINE__, info_level, "client EPOLLHUP");
		}
	}
	void server_type::on_server_event(socket_type * s, int events)
	{
		std::shared_ptr<socket_type> client = s->accept();
		if (client.get()) {
			if (shutdown) {
				client->shutdown(true, true);
				client->close();
				return;
			}
			lputs(__FILE__, __LINE__, info_level, "client connected");
			client->set_callback(client_event);
			client->set_blocking(false);
			client->set_extra(this);
			poll->append(client);
			client_type * ct = new client_type(client, password);
			clients[client.get()].reset(ct);
		} else {
			lprintf(__FILE__, __LINE__, info_level, "other events %x", events);
		}
	}
	bool client_type::parse(server_type * server)
	{
		while (true) {
			if (argument_count == 0) {
				std::string arg_count;
				if (!parse_line(arg_count)) {
					break;
				}
				if (!arg_count.empty() && *arg_count.begin() == '*') {
					argument_count = atoi(arg_count.c_str() + 1);
					argument_index = 0;
					if (argument_count <= 0) {
						lprintf(__FILE__, __LINE__, info_level, "unsupported protocol %s", arg_count.c_str());
						return false;
					}
					arguments.clear();
					arguments.resize(argument_count);
				} else {
					lprintf(__FILE__, __LINE__, info_level, "unsupported protocol %s", arg_count.c_str());
					return false;
				}
			} else if (argument_index < argument_count) {
				if (argument_size == argument_is_undefined) {
					std::string arg_size;
					if (!parse_line(arg_size)) {
						break;
					}
					if (!arg_size.empty() && *arg_size.begin() == '$') {
						argument_size = atoi(arg_size.c_str() + 1);
						if (argument_size < -1) {
							lprintf(__FILE__, __LINE__, info_level, "unsupported protocol %s", arg_size.c_str());
							return false;
						}
						if (argument_size < 0) {
							argument_size = argument_is_undefined;
							++argument_index;
						}
					} else {
						lprintf(__FILE__, __LINE__, info_level, "unsupported protocol %s", arg_size.c_str());
						return false;
					}
				} else {
					std::string arg_data;
					if (!parse_data(arg_data, argument_size)) {
						break;
					}
					auto & arg = arguments[argument_index];
					arg.first = arg_data;
					arg.second = true;
					argument_size = argument_is_undefined;
					++argument_index;
				}
			} else {
				if (!server->execute(this)) {
					response_error("ERR unknown");
				}
				arguments.clear();
				argument_count = 0;
				argument_index = 0;
				argument_size = argument_is_undefined;
			}
		}
		return true;
	}
	void client_type::response_status(const std::string & state)
	{
		const std::string & response = "+" + state + "\r\n";
		client->send(response.c_str(), response.size());
	}
	void client_type::response_error(const std::string & state)
	{
		const std::string & response = "-" + state + "\r\n";
		client->send(response.c_str(), response.size());
	}
	void client_type::response_ok()
	{
		static const std::string & response = "+OK\r\n";
		client->send(response.c_str(), response.size());
	}
	void client_type::response_pong()
	{
		static const std::string & response = "+PONG\r\n";
		client->send(response.c_str(), response.size());
	}
	void client_type::response_queued()
	{
		static const std::string & response = "+QUEUED\r\n";
		client->send(response.c_str(), response.size());
	}
	void client_type::response_integer0()
	{
		std::string response = ":0\r\n";
		client->send(response.c_str(), response.size());
	}
	void client_type::response_integer1()
	{
		std::string response = ":1\r\n";
		client->send(response.c_str(), response.size());
	}
	void client_type::response_integer(int64_t value)
	{
		std::string response = format(":%d\r\n", value);
		client->send(response.c_str(), response.size());
	}
	void client_type::response_bulk(const std::string & bulk, bool not_null)
	{
		if (not_null) {
			std::string response = format("$%d\r\n", bulk.size());
			client->send(response.c_str(), response.size());
			client->send(bulk.c_str(), bulk.size());
			response = "\r\n";
			client->send(response.c_str(), response.size());
		} else {
			response_null();
		}
	}
	void client_type::response_null()
	{
		static const std::string & response = "$-1\r\n";
		client->send(response.c_str(), response.size());
	}
	void client_type::response_null_multi_bulk()
	{
		static const std::string & response = "*-1\r\n";
		client->send(response.c_str(), response.size());
	}
	void client_type::response_start_multi_bulk(int count)
	{
		std::string response = format("*%d\r\n", count);
		client->send(response.c_str(), response.size());
	}
	void client_type::response_raw(const std::string & raw)
	{
		client->send(raw.c_str(), raw.size());
	}
	bool client_type::parse_line(std::string & line)
	{
		auto & buf = client->get_recv();
		auto it = std::find(buf.begin(), buf.end(), '\n');
		if (buf.end() != it) {
			if (it == buf.begin()) {//not found \r
				lputs(__FILE__, __LINE__, info_level, "not found CR");
				buf.pop_front();
			} else {
				auto prev = it;
				--prev;
				if (*prev != '\r') {//only \n
					++prev;
				}
				line.assign(buf.begin(), prev);
				++it;
				buf.erase(buf.begin(), it);
				return true;
			}
		}
		return false;
	}
	bool client_type::parse_data(std::string & data, int size)
	{
		auto & buf = client->get_recv();
		if (buf.size() < size + 2) {
			return false;
		}
		auto end = buf.begin() + size;
		data.assign(buf.begin(), end);
		buf.erase(buf.begin(), end + 2);
		return true;
	}
	bool server_type::execute(client_type * client)
	{
		try
		{
			auto & arguments = client->get_arguments();
			if (arguments.empty()) {
				throw std::runtime_error("ERR syntax error");
			}
			auto command = arguments.front().first;
			std::transform(command.begin(), command.end(), command.begin(), toupper);
			if (client->require_auth(command)) {
				throw std::runtime_error("NOAUTH Authentication required.");
			}
			if (client->queuing(command)) {
				client->response_queued();
				return true;
			}
			auto it = function_map.find(command);
			if (it != function_map.end()) {
				auto func = it->second;
				return ((this)->*func)(client);
			}
			lprintf(__FILE__, __LINE__, info_level, "not supported command %s", command.c_str());
		} catch (std::exception & e) {
			client->response_error(e.what());
			return true;
		} catch (...) {
			lputs(__FILE__, __LINE__, info_level, "unknown exception");
			return false;
		}
		return false;
	}
	void server_type::build_function_map()
	{
		//connection API
		function_map["AUTH"] = &server_type::function_auth;
		function_map["ECHO"] = &server_type::function_echo;
		function_map["PING"] = &server_type::function_ping;
		function_map["QUIT"] = &server_type::function_quit;
		function_map["SELECT"] = &server_type::function_select;
		//serve API
		function_map["DBSIZE"] = &server_type::function_dbsize;
		function_map["FLUSHALL"] = &server_type::function_flushall;
		function_map["FLUSHDB"] = &server_type::function_flushdb;
		function_map["SHUTDOWN"] = &server_type::function_shutdown;
		function_map["TIME"] = &server_type::function_time;
		//transaction API
		function_map["MULTI"] = &server_type::function_multi;
		function_map["EXEC"] = &server_type::function_exec;
		function_map["DISCARD"] = &server_type::function_discard;
		function_map["WATCH"] = &server_type::function_watch;
		function_map["UNWATCH"] = &server_type::function_unwatch;
		//keys API
		function_map["DEL"] = &server_type::function_del;
		function_map["EXISTS"] = &server_type::function_exists;
		function_map["EXPIRE"] = &server_type::function_expire;
		function_map["EXPIREAT"] = &server_type::function_expireat;
		function_map["PERSIST"] = &server_type::function_persist;
		function_map["TTL"] = &server_type::function_ttl;
		function_map["PTTL"] = &server_type::function_pttl;
		function_map["MOVE"] = &server_type::function_move;
		function_map["RANDOMKEY"] = &server_type::function_randomkey;
		function_map["RENAME"] = &server_type::function_rename;
		function_map["RENAMENX"] = &server_type::function_renamenx;
	}
	bool client_type::require_auth(const std::string & auth)
	{
		if (password.empty()) {
			return false;
		}
		if (auth == "AUTH" || auth == "QUIT") {
			return false;
		}
		return true;
	}
	bool client_type::auth(const std::string & password_)
	{
		if (password.empty()) {
			return false;
		}
		if (password == password_) {
			password.clear();
			return true;
		}
		return false;
	}
	///認証 
	///@param[in] password
	///@note Available since 1.0.0.
	bool server_type::function_auth(client_type * client)
	{
		if (!client->require_auth(std::string())) {
			throw std::runtime_error("ERR not required");
		}
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & password = arguments[1];
		if (!password.second || !client->auth(password.first)) {
			throw std::runtime_error("ERR not match");
		}
		client->response_ok();
		return true;
	}
	///エコー 
	///@param[in] message
	///@note Available since 1.0.0.
	bool server_type::function_echo(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & message = arguments[1];
		client->response_bulk(message.first, message.second);
		return true;
	}
	///Ping
	///@note Available since 1.0.0.
	bool server_type::function_ping(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 1) {
			throw std::runtime_error("ERR syntax error");
		}
		client->response_pong();
		return true;
	}
	///終了
	///@note Available since 1.0.0.
	bool server_type::function_quit(client_type * client)
	{
		client->response_status("OK");
		client->close_after_send();
		return true;
	}
	///データベース選択
	///@param[in] index
	///@note Available since 1.0.0.
	bool server_type::function_select(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 1) {
			throw std::runtime_error("ERR syntax error");
		}
		int index = atoi(arguments[1].first.c_str());
		if (index < 0 || databases.size() <= index) {
			throw std::runtime_error("ERR index out of range");
		}
		client->select(index);
		client->response_status("OK");
		return true;
	}
	///データベースのキー数取得 
	///@note Available since 1.0.0.
	bool server_type::function_dbsize(client_type * client)
	{
		auto & db = databases[client->get_db_index()];
		client->response_integer(db.get_dbsize());
		return true;
	}
	///データベースの全キー消去 
	///@note Available since 1.0.0.
	bool server_type::function_flushall(client_type * client)
	{
		for (auto it = databases.begin(), end = databases.end(); it != end; ++it) {
			it->clear();
		}
		client->response_ok();
		return true;
	}
	///選択しているデータベースの全キー消去 
	///@note Available since 1.0.0.
	bool server_type::function_flushdb(client_type * client)
	{
		auto & db = databases[client->get_db_index()];
		db.clear();
		client->response_ok();
		return true;
	}
	///サーバのシャットダウン
	///@note NOSAVE, SAVEオプションは無視する
	///@note Available since 1.0.0.
	bool server_type::function_shutdown(client_type * client)
	{
		lputs(__FILE__, __LINE__, info_level, "shutdown start");
		shutdown = true;
		client->response_ok();
		return true;
	}
	timeval_type::timeval_type()
	{
		update();
	}
	void timeval_type::update()
	{
		int r = gettimeofday(this, NULL);
		if (r < 0) {
			throw std::runtime_error("ERR could not get time");
		}
	}
	///サーバの時間
	///@note Available since 2.6.0.
	///@note Time complexity: O(1)
	bool server_type::function_time(client_type * client)
	{
		timeval_type tv;
		client->response_raw(format("*2\r\n:%d\r\n:%d\r\n", tv.tv_sec, tv.tv_usec));
		return true;
	}
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
	bool server_type::function_multi(client_type * client)
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
	bool server_type::function_exec(client_type * client)
	{
		//監視していた値が変更されていないか確認する
		//@todo マルチスレッドにするには確認しつつ、ロックを取得していく
		auto & watching = client->get_watching();
		for (auto it = watching.begin(), end = watching.end(); it != end; ++it) {
			auto & watch = *it;
			auto key = std::get<0>(watch);
			auto index = std::get<1>(watch);
			auto & db = databases[index];
			auto value = db.get(key);
			if (!value.get()) {
				client->response_null_multi_bulk();
				client->discard();
				return true;
			}
			auto watching_time = std::get<2>(watch);
			if (watching_time < value->last_modified_time) {
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
	bool server_type::function_discard(client_type * client)
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
	bool server_type::function_watch(client_type * client)
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
	bool server_type::function_unwatch(client_type * client)
	{
		client->unwatch();
		client->response_ok();
		return true;
	}
	///キーを削除する
	///@note Available since 1.0.0.
	bool server_type::function_del(client_type * client)
	{
		auto & arguments = client->get_arguments();
		int64_t removed = 0;
		auto & db = databases[client->get_db_index()];
		for (int i = 1, n = arguments.size(); i < n; ++i) {
			auto & key = arguments[i];
			if (key.second == false) {
				continue;
			}
			if (db.erase(key.first)) {
				++removed;
			}
		}
		client->response_integer(removed);
		return true;
	}
	///キーの存在確認
	///@note Available since 1.0.0.
	bool server_type::function_exists(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		if (key.second == false) {
			client->response_integer0();
			return true;
		}
		if (db.get(key.first).get()) {
			client->response_integer1();
		} else {
			client->response_integer0();
		}
		return true;
	}
	bool value_interface::is_expired()
	{
		return expiring && expire_time <= timeval_type();
	}
	void value_interface::expire(const timeval_type & at)
	{
		expiring = true;
		expire_time = at;
	}
	void value_interface::persist()
	{
		expiring = false;
	}
	///キーの有効期限設定
	///@note Available since 1.0.0.
	bool server_type::function_expire(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto value = db.get(key);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		timeval_type tv;
		tv.tv_sec += strtoll(arguments[2].first.c_str(), NULL, 10);
		value->expire(tv);
		client->response_integer1();
		//@todo expireするキーのリストを作っておき、それを過ぎたら消すようにしたい
		return true;
	}
	///キーの有効期限設定
	///@note Available since 1.0.0.
	bool server_type::function_expireat(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto value = db.get(key);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		timeval_type tv(strtoll(arguments[2].first.c_str(), NULL, 10), 0);
		value->expire(tv);
		client->response_integer1();
		//@todo expireするキーのリストを作っておき、それを過ぎたら消すようにしたい
		return true;
	}
	void timeval_type::add_msec(int64_t msec)
	{
		int64_t usec = tv_usec + (msec % 1000) * 1000;
		if (1000000 <= usec) {
			tv_usec = usec - 1000000;
			tv_sec += msec / 1000 + 1;
		} else {
			tv_sec += msec / 1000;
		}
	}
	///キーの有効期限設定
	///@note Available since 2.6.0.
	bool server_type::function_pexpire(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto value = db.get(key);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		timeval_type tv;
		tv.add_msec(strtoll(arguments[2].first.c_str(), NULL, 10));
		value->expire(tv);
		client->response_integer1();
		//@todo expireするキーのリストを作っておき、それを過ぎたら消すようにしたい
		return true;
	}
	///キーの有効期限設定
	///@note Available since 2.6.0.
	bool server_type::function_pexpireat(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto value = db.get(key);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		int64_t msec = strtoll(arguments[2].first.c_str(), NULL, 10);
		timeval_type tv(msec / 1000, (msec % 1000) * 1000);
		value->expire(tv);
		client->response_integer1();
		//@todo expireするキーのリストを作っておき、それを過ぎたら消すようにしたい
		return true;
	}
	///キーの有効期限消去
	///@note Available since 2.2.0.
	bool server_type::function_persist(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto value = db.get(key);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		value->persist();
		client->response_integer1();
		return true;
	}
	///キーの有効期限確認
	///@note Available since 1.0.0.
	bool server_type::function_ttl(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		timeval_type tv;
		auto value = db.get(key);
		if (!value.get()) {
			client->response_integer(-2);
			return true;
		}
		if (!value->is_expiring()) {
			client->response_integer(-1);
			return true;
		}
		tv -= value->expire_time;
		client->response_integer((tv.tv_sec * 2 + tv.tv_sec / 500000 + 1) / 2);
		return true;
	}
	///キーの有効期限確認
	///@note Available since 2.6.0.
	bool server_type::function_pttl(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		timeval_type tv;
		auto value = db.get(key);
		if (!value.get()) {
			client->response_integer(-2);
			return true;
		}
		if (!value->is_expiring()) {
			client->response_integer(-1);
			return true;
		}
		tv -= value->expire_time;
		client->response_integer(tv.tv_sec * 1000 + tv.tv_usec / 1000);
		return true;
	}
	///キーの移動
	///@note Available since 1.0.0.
	bool server_type::function_move(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto value = db.get(key);
		if (!value.get()) {
			client->response_integer0();
			return true;
		}
		int dst_index = atoi(arguments[2].first.c_str());
		if (dst_index == client->get_db_index()) {
			client->response_integer0();
			return 0;
		}
		auto & dst_db = databases[dst_index];
		if (dst_db.get(key).get()) {
			client->response_integer0();
			return true;
		}
		if (!dst_db.insert(key.first, value)) {
			client->response_integer0();
			return false;
		}
		db.erase(key.first);
		client->response_integer1();
		return true;
	}
	///ランダムなキーの取得
	///@note Available since 1.0.0.
	bool server_type::function_randomkey(client_type * client)
	{
		auto & db = databases[client->get_db_index()];
		auto value = db.randomkey();
		if (value.empty()) {
			client->response_null();
		} else {
			client->response_bulk(value);
		}
		return true;
	}
	///キー名の変更
	///@note Available since 1.0.0.
	bool server_type::function_rename(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto value = db.get(key);
		if (!value.get()) {
			throw std::runtime_error("ERR not exist");
		}
		auto newkey = arguments[2].first;
		if (key.first == newkey) {
			throw std::runtime_error("ERR same key");
		}
		db.erase(key.first);
		db.insert(newkey, value);
		client->response_ok();
		return true;
	}
	///キー名の変更(上書き不可)
	///@note Available since 1.0.0.
	bool server_type::function_renamenx(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 3) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & db = databases[client->get_db_index()];
		auto & key = arguments[1];
		auto value = db.get(key);
		if (!value.get()) {
			throw std::runtime_error("ERR not exist");
		}
		auto newkey = arguments[2];
		if (key.first == newkey.first) {
			throw std::runtime_error("ERR same key");
		}
		auto dst_value = db.get(newkey);
		if (dst_value.get()) {
			client->response_integer0();
			return true;
		}
		db.erase(key.first);
		db.insert(newkey.first, value);
		client->response_integer1();
		return true;
	}
}
