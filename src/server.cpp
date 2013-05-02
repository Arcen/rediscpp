#include "server.h"
#include "log.h"
#include <algorithm>
#include <ctype.h>
#include <signal.h>

namespace rediscpp
{
	server_type::server_type()
		: shutdown(false)
	{
		signal(SIGPIPE, SIG_IGN);
		databases.resize(1);
		for (auto it = databases.begin(), end = databases.end(); it != end; ++it) {
			it->reset(new database_type());
		}
		build_api_map();
	}
	client_type::client_type(server_type & server_, std::shared_ptr<socket_type> & client_, const std::string & password_)
		: server(server_)
		, client(client_)
		, argument_count(0)
		, argument_index(0)
		, argument_size(argument_is_undefined)
		, password(password_)
		, db_index(0)
		, transaction(false)
		, current_time(0, 0)
	{
		write_cache.reserve(1500);
	}
	bool server_type::start(const std::string & hostname, const std::string & port, int threads)
	{
		std::shared_ptr<address_type> addr(new address_type);
		addr->set_hostname(hostname.c_str());
		addr->set_port(atoi(port.c_str()));
		listening = socket_type::create(*addr);
		listening->set_reuse();
		if (!listening->bind(addr)) {
			return false;
		}
		if (!listening->listen(512)) {
			return false;
		}
		listening->set_extra(this);
		listening->set_callback(server_callback);
		listening->set_nonblocking();
		poll = poll_type::create();
		poll->append(listening);
		int base_poll_count = 1;
		if (threads) {
			event = event_type::create();
			event->set_extra(this);
			event->set_callback(event_callback);
			poll->append(event);
			++base_poll_count;

			startup_threads(threads);
		}
		while (true) {
			try {
				process();
				//メインスレッドだけの機能
				if (shutdown) {
					if (poll->get_count() == base_poll_count) {
						lputs(__FILE__, __LINE__, info_level, "quit server, no client now");
						break;
					}
				}
				//ガベージコレクト
				{
					timeval_type tv;
					for (int i = 0, n = databases.size(); i < n; ++i) {
						auto db = writable_db(i);
						db->flush_expiring_key(tv);
					}
				}
			} catch (std::exception e) {
				lprintf(__FILE__, __LINE__, info_level, "exception:%s", e.what());
			} catch (...) {
				lputs(__FILE__, __LINE__, info_level, "exception");
			}
		}
		if (thread_pool.size()) {
		}
		return true;
	}
	void server_type::client_callback(pollable_type * p, int events)
	{
		socket_type * s = dynamic_cast<socket_type *>(p);
		if (!s) {
			return;
		}
		server_type * server = reinterpret_cast<server_type *>(s->get_extra());
		if (!server) {
			return;
		}
		server->on_client(s, events);
	}
	void server_type::server_callback(pollable_type * p, int events)
	{
		socket_type * s = dynamic_cast<socket_type *>(p);
		if (!s) {
			return;
		}
		server_type * server = reinterpret_cast<server_type *>(s->get_extra());
		if (!server) {
			return;
		}
		server->on_server(s, events);
	}
	void server_type::event_callback(pollable_type * p, int events)
	{
		event_type * e = dynamic_cast<event_type *>(p);
		if (!e) {
			return;
		}
		server_type * server = reinterpret_cast<server_type *>(e->get_extra());
		if (!server) {
			return;
		}
		server->on_event(e, events);
	}
	void server_type::on_server(socket_type * s, int events)
	{
		std::shared_ptr<socket_type> cs = s->accept();
		if (!cs.get()) {
			return;
		}
		//新規受け付けは停止中
		if (shutdown) {
			cs->shutdown(true, true);
			cs->close();
			return;
		}
		cs->set_callback(client_callback);
		cs->set_nonblocking();
		cs->set_nodelay();
		std::shared_ptr<client_type> ct(new client_type(*this, cs, password));
		ct->set(ct);
		cs->set_extra(this);
		cs->set_extra2(ct.get());
		ct->process();
		if (cs->done()) {
			cs->close();
		} else {
			//１回の接続で送受信が終わってない場合
			if (thread_pool.empty()) {
				poll->append(cs);
				clients[cs.get()] = ct;
			} else {
				std::shared_ptr<job_type> job(new job_type(job_type::add_type, ct));
				jobs.push(job);
				event->send();
			}
		}
	}
	void server_type::on_event(event_type * e, int events)
	{
		e->recv();
		while (true) {
			auto job = jobs.pop(0);
			if (!job) {
				break;
			}
			switch (job->type) {
			case job_type::add_type:
				{
					auto client = job->client;
					auto cs = client->client;
					poll->append(cs);
					clients[cs.get()] = client;
				}
				break;
			case job_type::del_type:
				{
					auto client = job->client;
					auto cs = client->client;
					cs->close();
					clients.erase(cs.get());
				}
				break;
			}
		}
		e->mod();
	}
	void server_type::on_client(socket_type * s, int events)
	{
		std::shared_ptr<client_type> client = reinterpret_cast<client_type *>(s->get_extra2())->get();
		if ((events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) || s->is_broken()) {
			//lprintf(__FILE__, __LINE__, info_level, "client closed");
			remove_client(client);
			return;
		}
		client->events = events;
		client->process();
		if (s->done()) {
			remove_client(client);
		} else {
			s->mod();
		}
	}
	void server_type::remove_client(std::shared_ptr<client_type> client)
	{
		if (thread_pool.empty()) {
			client->client->close();
			clients.erase(client->client.get());
		} else {
			std::shared_ptr<job_type> job(new job_type(job_type::del_type, client));
			jobs.push(job);
			event->send();
		}
	}
	void client_type::process()
	{
		if (events & EPOLLIN) {//recv
			//lputs(__FILE__, __LINE__, info_level, "client EPOLLIN");
			client->recv();
			if (client->should_recv()) {
				parse();
			}
			if (client->done()) {
				return;
			}
			if (client->should_send()) {
				client->send();
			}
		} else if (events & EPOLLOUT) {//send
			client->send();
		}
	}
	void client_type::inline_command_parser(const std::string & line)
	{
		size_t end = line.size();
		for (size_t offset = 0; offset < end && offset != line.npos;) {
			size_t space = line.find(' ', offset);
			if (space != line.npos) {
				arguments.push_back(line.substr(offset, space - offset));
				offset = line.find_first_not_of(' ', space + 1);
			} else {
				arguments.push_back(line.substr(offset));
				break;
			}
		}
	}
	bool client_type::parse()
	{
		bool time_updated = false;
		while (true) {
			if (argument_count == 0) {
				std::string arg_count;
				if (!parse_line(arg_count)) {
					break;
				}
				if (arg_count.empty()) {
					continue;
				}
				if (*arg_count.begin() == '*') {
					argument_count = atoi(arg_count.c_str() + 1);
					argument_index = 0;
					if (argument_count <= 0) {
						lprintf(__FILE__, __LINE__, info_level, "unsupported protocol %s", arg_count.c_str());
						flush();
						return false;
					}
					arguments.clear();
					arguments.resize(argument_count);
				} else {
					inline_command_parser(arg_count);
					if (time_updated) {
						time_updated = true;
						current_time.update();
					}
					if (!execute()) {
						response_error("ERR unknown");
					}
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
							flush();
							return false;
						}
						if (argument_size < 0) {
							argument_size = argument_is_undefined;
							++argument_index;
						}
					} else {
						lprintf(__FILE__, __LINE__, info_level, "unsupported protocol %s", arg_size.c_str());
						flush();
						return false;
					}
				} else {
					std::string arg_data;
					if (!parse_data(arg_data, argument_size)) {
						break;
					}
					auto & arg = arguments[argument_index];
					arg = arg_data;
					argument_size = argument_is_undefined;
					++argument_index;
				}
			} else {
				if (time_updated) {
					time_updated = true;
					current_time.update();
				}
				if (!execute()) {
					response_error("ERR unknown");
				}
				arguments.clear();
				argument_count = 0;
				argument_index = 0;
				argument_size = argument_is_undefined;
			}
		}
		flush();
		return true;
	}
	void client_type::response_status(const std::string & state)
	{
		response_raw("+" + state + "\r\n");
	}
	void client_type::response_error(const std::string & state)
	{
		response_raw("-" + state + "\r\n");
	}
	void client_type::response_ok()
	{
		response_raw("+OK\r\n");
	}
	void client_type::response_pong()
	{
		response_raw("+PONG\r\n");
	}
	void client_type::response_queued()
	{
		response_raw("+QUEUED\r\n");
	}
	void client_type::response_integer0()
	{
		response_raw(":0\r\n");
	}
	void client_type::response_integer1()
	{
		response_raw(":1\r\n");
	}
	void client_type::response_integer(int64_t value)
	{
		response_raw(format(":%d\r\n", value));
	}
	void client_type::response_bulk(const std::string & bulk, bool not_null)
	{
		if (not_null) {
			response_raw(format("$%d\r\n", bulk.size()));
			response_raw(bulk);
			response_raw("\r\n");
		} else {
			response_null();
		}
	}
	void client_type::response_null()
	{
		response_raw("$-1\r\n");
	}
	void client_type::response_null_multi_bulk()
	{
		response_raw("*-1\r\n");
	}
	void client_type::response_start_multi_bulk(int count)
	{
		response_raw(format("*%d\r\n", count));
	}
	void client_type::response_raw(const std::string & raw)
	{
		if (raw.size() <= write_cache.capacity() - write_cache.size()) {
			write_cache.insert(write_cache.end(), raw.begin(), raw.end());
		} else if (raw.size() <= write_cache.capacity()) {
			flush();
			write_cache.insert(write_cache.end(), raw.begin(), raw.end());
		} else {
			flush();
			client->send(raw.c_str(), raw.size());
		}
	}
	void client_type::flush()
	{
		if (!write_cache.empty()) {
			client->send(&write_cache[0], write_cache.size());
			write_cache.clear();
		}
	}
	bool client_type::parse_line(std::string & line)
	{
		auto & buf = client->get_recv();
		if (buf.size() < 2) {
			return false;
		}
		auto begin = buf.begin();
		auto end = buf.end();
		--end;
		auto it = std::find(begin, end, '\r');
		if (it != end) {
			line.assign(begin, it);
			std::advance(it, 2);
			buf.erase(begin, it);
			return true;
		}
		return false;
	}
	bool client_type::parse_data(std::string & data, int size)
	{
		auto & buf = client->get_recv();
		if (buf.size() < size + 2) {
			return false;
		}
		auto begin = buf.begin();
		auto end = begin;
		std::advance(end, size);
		data.assign(begin, end);
		std::advance(end, 2);
		buf.erase(begin, end);
		return true;
	}
	bool client_type::execute()
	{
		try
		{
			if (arguments.empty()) {
				throw std::runtime_error("ERR syntax error");
			}
			auto & command = arguments.front();
			std::transform(command.begin(), command.end(), command.begin(), toupper);
			if (require_auth(command)) {
				throw std::runtime_error("NOAUTH Authentication required.");
			}
			if (queuing(command)) {
				response_queued();
				return true;
			}
			auto it = server.api_map.find(command);
			if (it != server.api_map.end()) {
				auto info = it->second;
				return (server.*(info.function))(this);
			}
			//lprintf(__FILE__, __LINE__, info_level, "not supported command %s", command.c_str());
		} catch (std::exception & e) {
			response_error(e.what());
			return true;
		} catch (...) {
			lputs(__FILE__, __LINE__, info_level, "unknown exception");
			return false;
		}
		return false;
	}
	void server_type::build_api_map()
	{
		//connection API
		api_map["AUTH"].set(&server_type::api_auth);
		api_map["ECHO"].set(&server_type::api_echo);
		api_map["PING"].set(&server_type::api_ping);
		api_map["QUIT"].set(&server_type::api_quit);
		api_map["SELECT"].set(&server_type::api_select);
		//serve API
		api_map["DBSIZE"].set(&server_type::api_dbsize);
		api_map["FLUSHALL"].set(&server_type::api_flushall);
		api_map["FLUSHDB"].set(&server_type::api_flushdb);
		api_map["SHUTDOWN"].set(&server_type::api_shutdown);
		api_map["TIME"].set(&server_type::api_time);
		//transaction API
		api_map["MULTI"].set(&server_type::api_multi);
		api_map["EXEC"].set(&server_type::api_exec);
		api_map["DISCARD"].set(&server_type::api_discard);
		api_map["WATCH"].set(&server_type::api_watch);
		api_map["UNWATCH"].set(&server_type::api_unwatch);
		//keys API
		api_map["KEYS"].set(&server_type::api_keys);
		api_map["DEL"].set(&server_type::api_del);
		api_map["EXISTS"].set(&server_type::api_exists);
		api_map["EXPIRE"].set(&server_type::api_expire);
		api_map["EXPIREAT"].set(&server_type::api_expireat);
		api_map["PERSIST"].set(&server_type::api_persist);
		api_map["TTL"].set(&server_type::api_ttl);
		api_map["PTTL"].set(&server_type::api_pttl);
		api_map["MOVE"].set(&server_type::api_move);
		api_map["RANDOMKEY"].set(&server_type::api_randomkey);
		api_map["RENAME"].set(&server_type::api_rename);
		api_map["RENAMENX"].set(&server_type::api_renamenx);
		api_map["TYPE"].set(&server_type::api_type);
		//strings api
		api_map["GET"].set(&server_type::api_get);
		api_map["SET"].set(&server_type::api_set);
		api_map["SETEX"].set(&server_type::api_setex);
		api_map["SETNX"].set(&server_type::api_setnx);
		api_map["PSETEX"].set(&server_type::api_psetex);
		api_map["STRLEN"].set(&server_type::api_strlen);
		api_map["APPEND"].set(&server_type::api_append);
		api_map["GETRANGE"].set(&server_type::api_getrange);
		api_map["SUBSTR"].set(&server_type::api_getrange);
		api_map["SETRANGE"].set(&server_type::api_setrange);
		/*
		api_map["GETSET"].set(&server_type::api_getset);
		api_map["MGET"].set(&server_type::api_mget);
		api_map["MSET"].set(&server_type::api_mset);
		api_map["MSETNX"].set(&server_type::api_msetnx);
		api_map["DECR"].set(&server_type::api_decr);
		api_map["DECRBY"].set(&server_type::api_decrby);
		api_map["INCR"].set(&server_type::api_incr);
		api_map["INCRBY"].set(&server_type::api_incrby);
		api_map["INCRBYFLOAT"].set(&server_type::api_incrbyfloat);
		api_map["BITCOUNT"].set(&server_type::api_bitcount);
		api_map["BITOP"].set(&server_type::api_bitop);
		api_map["GETBIT"].set(&server_type::api_getbit);
		api_map["SETBIT"].set(&server_type::api_setbit);*/
	}
	server_type::~server_type()
	{
		shutdown_threads();
	}
	void server_type::startup_threads(int threads)
	{
		thread_pool.resize(threads);
		for (auto it = thread_pool.begin(), end = thread_pool.end(); it != end; ++it) {
			it->reset(new worker_type(*this));
			(*it)->craete();
		}
	}
	void server_type::shutdown_threads()
	{
		if (thread_pool.empty()) {
			return;
		}
		for (auto it = thread_pool.begin(), end = thread_pool.end(); it != end; ++it) {
			auto & thread = *it;
			thread->shutdown();
		}
		//@todo レベルトリガの別イベントを発生させて、終了を通知しても良い
		for (auto it = thread_pool.begin(), end = thread_pool.end(); it != end; ++it) {
			auto & thread = *it;
			thread->join();
		}
		thread_pool.clear();
	}
	void server_type::process()
	{
		try
		{
			std::vector<epoll_event> events(1);
			poll->wait(events, 1000);
			for (auto it = events.begin(), end = events.end(); it != end; ++it) {
				auto pollable = reinterpret_cast<pollable_type*>(it->data.ptr);
				if (pollable) {
					pollable->trigger(it->events);
				}
			}
		} catch (std::exception e) {
			lprintf(__FILE__, __LINE__, info_level, "exception %s", e.what());
		} catch (...) {
			lprintf(__FILE__, __LINE__, info_level, "exception");
		}
	}
	worker_type::worker_type(server_type & server_)
		: server(server_)
	{
	}
	void worker_type::run()
	{
		server.process();
	}
	database_write_locker::database_write_locker(database_type * database_)
		: database(database_)
		, locker(new rwlock_locker(database_->rwlock, write_lock_type))
	{
	}
	database_read_locker::database_read_locker(database_type * database_)
		: database(database_)
		, locker(new rwlock_locker(database_->rwlock, read_lock_type))
	{
	}
}
