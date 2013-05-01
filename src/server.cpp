#include "server.h"
#include "log.h"
#include <algorithm>
#include <ctype.h>

namespace rediscpp
{
	server_type::server_type()
		: shutdown(false)
		, thread_pool_mutex(true)
		, thread_pool_cond(thread_pool_mutex)
	{
		databases.resize(1);
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
		, finished(false)
	{
		write_cache.reserve(1500);
	}
	bool server_type::start(const std::string & hostname, const std::string & port, int threads)
	{
		thread_pool.resize(threads);
		startup_threads();
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
		listening->set_callback(server_event);
		listening->set_extra(this);
		listening->set_nonblocking();
		poll = poll_type::create();
		poll->append(listening);
		while (true) {
			try {
				thread_withdraw();
				poll->wait(10);
				if (shutdown) {
					if (poll->get_count() == 1) {
						lputs(__FILE__, __LINE__, info_level, "quit server, no client now");
						break;
					}
				}
			} catch (std::exception e) {
				lprintf(__FILE__, __LINE__, info_level, "exception:%s", e.what());
			} catch (...) {
				lputs(__FILE__, __LINE__, info_level, "exception");
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
		if ((events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) || s->is_broken()) {
			//lprintf(__FILE__, __LINE__, info_level, "client closed");
			s->close();
			clients.erase(s);
			return;
		}
		if (!thread_pool.empty()) {
			if (events & (EPOLLIN|EPOLLOUT)) {
				auto & client = clients[s];
				poll->remove(client->client);
				client->events = events;
				mutex_locker locker(thread_pool_mutex);
				task_queue.push_back(client);
				thread_pool_cond.signal();
			}
		} else {
			auto client = clients[s];
			client->events = events;
			client->process();
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
			if (client->recv_done() && !client->should_send()) {
				finish();
				return;
			}
			if (client->should_send()) {
				client->send();
			}
		} else if (events & EPOLLOUT) {//send
			client->send();
		}
	}
	void server_type::on_server_event(socket_type * s, int events)
	{
		while (true) {
			std::shared_ptr<socket_type> client = s->accept();
			if (!client.get()) {
				return;
			}
			if (shutdown) {
				client->shutdown(true, true);
				client->close();
				continue;
			}
			//lputs(__FILE__, __LINE__, info_level, "client connected");
			client->set_callback(client_event);
			client->set_nonblocking();
			client->set_extra(this);
			client->set_nodelay();
			poll->append(client);
			client_type * ct = new client_type(*this, client, password);
			clients[client.get()].reset(ct);
		}
	}
	void inline_command_parser(arguments_type & arguments, const std::string & line)
	{
		size_t end = line.size();
		for (size_t offset = 0; offset < end && offset != line.npos;) {
			size_t space = line.find(' ', offset);
			if (space != line.npos) {
				arguments.push_back(std::make_pair(line.substr(offset, space - offset), true));
				offset = line.find_first_not_of(' ', space + 1);
			} else {
				arguments.push_back(std::make_pair(line.substr(offset), true));
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
					inline_command_parser(arguments, arg_count);
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
					arg.first = arg_data;
					arg.second = true;
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
			auto command = arguments.front().first;
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
				rwlock_locker locker(server.db_lock, info.lock_type);
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
		api_map["AUTH"].set(&server_type::api_auth, no_lock_type);
		api_map["ECHO"].set(&server_type::api_echo, no_lock_type);
		api_map["PING"].set(&server_type::api_ping, no_lock_type);
		api_map["QUIT"].set(&server_type::api_quit, no_lock_type);
		api_map["SELECT"].set(&server_type::api_select, no_lock_type);
		//serve API
		api_map["DBSIZE"].set(&server_type::api_dbsize, write_lock_type);
		api_map["FLUSHALL"].set(&server_type::api_flushall, write_lock_type);
		api_map["FLUSHDB"].set(&server_type::api_flushdb, write_lock_type);
		api_map["SHUTDOWN"].set(&server_type::api_shutdown, write_lock_type);
		api_map["TIME"].set(&server_type::api_time, no_lock_type);
		//transaction API
		api_map["MULTI"].set(&server_type::api_multi, write_lock_type);
		api_map["EXEC"].set(&server_type::api_exec, write_lock_type);
		api_map["DISCARD"].set(&server_type::api_discard, write_lock_type);
		api_map["WATCH"].set(&server_type::api_watch, write_lock_type);
		api_map["UNWATCH"].set(&server_type::api_unwatch, write_lock_type);
		//keys API
		api_map["DEL"].set(&server_type::api_del, write_lock_type);
		api_map["EXISTS"].set(&server_type::api_exists, write_lock_type);
		api_map["EXPIRE"].set(&server_type::api_expire, write_lock_type);
		api_map["EXPIREAT"].set(&server_type::api_expireat, write_lock_type);
		api_map["PERSIST"].set(&server_type::api_persist, write_lock_type);
		api_map["TTL"].set(&server_type::api_ttl, write_lock_type);
		api_map["PTTL"].set(&server_type::api_pttl, write_lock_type);
		api_map["MOVE"].set(&server_type::api_move, write_lock_type);
		api_map["RANDOMKEY"].set(&server_type::api_randomkey, write_lock_type);
		api_map["RENAME"].set(&server_type::api_rename, write_lock_type);
		api_map["RENAMENX"].set(&server_type::api_renamenx, write_lock_type);
		api_map["TYPE"].set(&server_type::api_type, write_lock_type);
		//strings api
		api_map["GET"].set(&server_type::api_get, read_lock_type);
		api_map["SET"].set(&server_type::api_set, write_lock_type);
		api_map["SETEX"].set(&server_type::api_setex, write_lock_type);
		api_map["SETNX"].set(&server_type::api_setnx, write_lock_type);
		api_map["PSETEX"].set(&server_type::api_psetex, write_lock_type);
		api_map["STRLEN"].set(&server_type::api_strlen, write_lock_type);
	}
	server_type::~server_type()
	{
		shutdown_threads();
	}
	void server_type::startup_threads()
	{
		for (auto it = thread_pool.begin(), end = thread_pool.end(); it != end; ++it) {
			it->reset(new client_thread_type(*this));
			(*it)->craete();
		}
	}
	void server_type::shutdown_threads()
	{
		for (auto it = thread_pool.begin(), end = thread_pool.end(); it != end; ++it) {
			auto & thread = *it;
			thread->shutdown();
		}
		{
			mutex_locker locker(thread_pool_mutex);
			thread_pool_cond.broadcast();
		}
		for (auto it = thread_pool.begin(), end = thread_pool.end(); it != end; ++it) {
			auto & thread = *it;
			thread->join();
		}
		thread_pool.clear();
	}
	client_thread_type::client_thread_type(server_type & server_)
		: server(server_)
	{
	}
	std::shared_ptr<client_type> server_type::thread_wait()
	{
		mutex_locker locker(thread_pool_mutex);
		if (task_queue.empty()) {
			thread_pool_cond.timedwait(1000000);
			//thread_pool_cond.wait();
			if (task_queue.empty()) {
				return std::shared_ptr<client_type>();
			}
		}
		std::shared_ptr<client_type> client = task_queue.front();
		task_queue.pop_front();
		return client;
	}
	void server_type::thread_return(std::shared_ptr<client_type> client)
	{
		mutex_locker locker(thread_pool_mutex);
		return_queue.push_back(client);
	}
	void server_type::thread_withdraw()
	{
		mutex_locker locker(thread_pool_mutex);
		while (!return_queue.empty()) {
			auto client = return_queue.front();
			return_queue.pop_front();
			if (client->is_finished()) {
				client->client->close();
				clients.erase(client->client.get());
				//lprintf(__FILE__, __LINE__, info_level, "client closed %d", clients.size());
			} else {
				poll->append(client->client);
			}
		}
	}
	void client_thread_type::run()
	{
		try
		{
			//外部からのタイミング待ち
			auto client = server.thread_wait();
			if (client.get()) {
				client->process();
				server.thread_return(client);
			}
		} catch (std::exception e) {
			lprintf(__FILE__, __LINE__, info_level, "exception %s", e.what());
		} catch (...) {
			lprintf(__FILE__, __LINE__, info_level, "exception");
		}
	}
}
