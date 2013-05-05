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
		bits_table.resize(256);
		for (int i = 0; i < 256; ++i) {
			int count = 0;
			for (int j = 0; j < 8; ++j) {
				if (i & (1 << j)) {
					++count;
				}
			}
			bits_table[i] = count;
		}
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
		, multi_executing(false)
		, current_time(0, 0)
		, blocked(false)
		, blocked_till(0, 0)
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

		timer = timer_type::create();
		timer->set_extra(this);
		timer->set_callback(timer_callback);

		event = event_type::create();
		event->set_extra(this);
		event->set_callback(event_callback);

		poll = poll_type::create();
		poll->append(listening);
		poll->append(timer);
		poll->append(event);

		const int base_poll_count = 3;//listening & timer
		if (threads) {
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
						auto db = writable_db(i, NULL);
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
	void server_type::timer_callback(pollable_type * p, int events)
	{
		timer_type * t = dynamic_cast<timer_type *>(p);
		if (!t) {
			return;
		}
		server_type * server = reinterpret_cast<server_type *>(t->get_extra());
		if (!server) {
			return;
		}
		server->on_timer(t, events);
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
			append_client(ct);
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
				append_client(job->client, true);
				break;
			case job_type::del_type:
				remove_client(job->client, true);
				break;
			case job_type::list_pushed_type:
				excecute_blocked_client(true);
				break;
			}
		}
		e->mod();
	}
	void server_type::on_timer(timer_type * t, int events)
	{
		if (t->recv()) {
			excecute_blocked_client();
		}
		t->mod();
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
		} else if (!client->is_blocked()) {
			s->mod();
		}
	}
	void server_type::append_client(std::shared_ptr<client_type> client, bool now)
	{
		if (thread_pool.empty() || now) {
			poll->append(client->client);
			clients[client->client.get()] = client;
		} else {
			std::shared_ptr<job_type> job(new job_type(job_type::add_type, client));
			jobs.push(job);
			event->send();
		}
	}
	void server_type::remove_client(std::shared_ptr<client_type> client, bool now)
	{
		if (thread_pool.empty() || now) {
			if (client->is_blocked()) {
				unblocked(client);
			}
			client->client->close();
			clients.erase(client->client.get());
		} else {
			std::shared_ptr<job_type> job(new job_type(job_type::del_type, client));
			jobs.push(job);
			event->send();
		}
	}
	void server_type::excecute_blocked_client(bool now)
	{
		if (thread_pool.empty() || now) {
			std::set<std::shared_ptr<client_type>> clients;
			{
				mutex_locker locker(blocked_mutex);
				clients = blocked_clients;
			}
			bool set_timer = false;
			timeval_type tv;
			for (auto it = clients.begin(), end = clients.end(); it != end; ++it) {
				auto & client = *it;
				client->process();
				if (client->is_blocked()) {
					lprintf(__FILE__, __LINE__, debug_level, "still blocked");
					timeval_type ctv = client->get_blocked_till();
					if (!ctv.is_epoc()) {
						if (!set_timer || ctv < tv) {
							set_timer = true;
							if (client->current_time < ctv) {
								tv = ctv - client->current_time;
							}
						}
					}
				}
			}
			//次にタイムアウトが起きるクライアントを起こすイベントを設定する
			if (set_timer) {
				if (tv.tv_sec == 0 && tv.tv_usec == 0) {
					tv.tv_usec = 1;
				}
				timer->start(tv.tv_sec, tv.tv_usec * 1000, true);
			}
		} else {
			notify_list_pushed();
		}
	}
	void server_type::notify_list_pushed()
	{
		std::shared_ptr<job_type> job(new job_type(job_type::list_pushed_type, std::shared_ptr<client_type>()));
		jobs.push(job);
		event->send();
	}
	void client_type::process()
	{
		if (is_blocked()) {
			parse();
			if (client->should_send()) {
				client->send();
			}
			if (!is_blocked()) {
				client->mod();
			}
			return;
		}
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
		try {
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
						argument_index = argument_count = arg_count.size();
						if (!time_updated) {
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
					if (!time_updated) {
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
		} catch (blocked_exception e) {
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
		response_raw(format(":%"PRId64"\r\n", value));
	}
	void client_type::response_bulk(const std::string & bulk, bool not_null)
	{
		if (not_null) {
			response_raw(format("$%zd\r\n", bulk.size()));
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
	void client_type::response_start_multi_bulk(size_t count)
	{
		response_raw(format("*%zd\r\n", count));
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
				return execute(it->second);
			}
			//lprintf(__FILE__, __LINE__, info_level, "not supported command %s", command.c_str());
		} catch (blocked_exception & e) {
			throw;
		} catch (std::exception & e) {
			response_error(e.what());
			return true;
		} catch (...) {
			lputs(__FILE__, __LINE__, info_level, "unknown exception");
			return false;
		}
		return false;
	}
	bool client_type::execute(const api_info & info)
	{
		//引数確認
		if (arguments.size() < info.min_argc) {
			response_error("ERR syntax error too few arguments");
			return true;
		}
		if (info.max_argc < arguments.size()) {
			response_error("ERR syntax error too much arguments");
			return true;
		}
		try
		{
			size_t argc = arguments.size();
			size_t pattern_length = info.arg_types.size();
			size_t arg_pos = 0;
			keys.clear();
			values.clear();
			members.clear();
			fields.clear();
			scores.clear();
			keys.reserve(argc);
			values.reserve(argc);
			members.reserve(argc);
			fields.reserve(argc);
			scores.reserve(argc);
			for (; arg_pos < pattern_length && arg_pos < argc; ++arg_pos) {
				if (info.arg_types[arg_pos] == '*') {
					break;
				}
				switch (info.arg_types[arg_pos]) {
				case 'c'://command
				case 't'://time
				case 'n'://numeric
					break;
				case 'k'://key
					keys.push_back(&arguments[arg_pos]);
					break;
				case 'v'://value
					values.push_back(&arguments[arg_pos]);
					break;
				case 'f'://field
					fields.push_back(&arguments[arg_pos]);
					break;
				case 'm'://member
					members.push_back(&arguments[arg_pos]);
					break;
				case 's'://scre
					scores.push_back(&arguments[arg_pos]);
					break;
				case 'd'://db index
					{
						int64_t index = atoi64(arguments[arg_pos]);
						if (index < 0 || server.databases.size() <= index) {
							throw std::runtime_error("ERR db index is wrong range");
						}
					}
					break;
				}
			}
			//後方一致
			std::list<std::string*> back_keys;
			std::list<std::string*> back_values;
			std::list<std::string*> back_fields;
			std::list<std::string*> back_members;
			std::list<std::string*> back_scores;
			if (arg_pos < argc && arg_pos < pattern_length && info.arg_types[arg_pos] == '*') {
				for (; 0 < argc && arg_pos < pattern_length; --argc, --pattern_length) {
					if (info.arg_types[pattern_length-1] == '*') {
						break;
					}
					switch (info.arg_types[pattern_length-1]) {
					case 'c'://command
					case 't'://time
					case 'n'://numeric
						break;
					case 'k'://key
						back_keys.push_front(&arguments[argc-1]);
						break;
					case 'v'://value
						back_values.push_front(&arguments[argc-1]);
						break;
					case 'f'://field
						back_fields.push_back(&arguments[argc-1]);
						break;
					case 'm'://member
						back_members.push_back(&arguments[argc-1]);
						break;
					case 's'://scre
						back_scores.push_back(&arguments[argc-1]);
						break;
					case 'd'://db index
						{
							int64_t index = atoi64(arguments[argc-1]);
							if (index < 0 || server.databases.size() <= index) {
								throw std::runtime_error("ERR db index is wrong range");
							}
						}
						break;
					}
				}
			}
			if (arg_pos < argc) {
				size_t star_count = 0;
				for (size_t i = arg_pos; i < pattern_length; ++i) {
					if (info.arg_types[i] == '*') {
						++star_count;
					} else {
						throw std::runtime_error("ERR syntax error too few arguments");
					}
				}
				if (!star_count || arg_pos < star_count) {
					throw std::runtime_error("ERR command structure error");
				}
				if ((argc - arg_pos) % star_count != 0) {
					throw std::runtime_error("ERR syntax error");
				}
				for (size_t s = 0; s < star_count; ++s) {
					switch (info.arg_types[arg_pos+s-star_count]) {
					case 'k':
						for (size_t pos = arg_pos + s; pos < argc; pos += star_count) {
							keys.push_back(&arguments[pos]);
							//lprintf(__FILE__, __LINE__, info_level, "set key %s", arguments[pos].c_str());
						}
						break;
					case 'v':
						for (size_t pos = arg_pos + s; pos < argc; pos += star_count) {
							values.push_back(&arguments[pos]);
							//lprintf(__FILE__, __LINE__, info_level, "set value %s", arguments[pos].c_str());
						}
						break;
					case 'f':
						for (size_t pos = arg_pos + s; pos < argc; pos += star_count) {
							fields.push_back(&arguments[pos]);
						}
						break;
					case 'm':
						for (size_t pos = arg_pos + s; pos < argc; pos += star_count) {
							members.push_back(&arguments[pos]);
						}
						break;
					case 's':
						for (size_t pos = arg_pos + s; pos < argc; pos += star_count) {
							scores.push_back(&arguments[pos]);
						}
						break;
					default:
						throw std::runtime_error("ERR command pattern error");
					}
				}
			}
			if (!back_keys.empty()) keys.insert(keys.end(), back_keys.begin(), back_keys.end());
			if (!back_values.empty()) values.insert(values.end(), back_values.begin(), back_values.end());
			if (!back_fields.empty()) fields.insert(fields.end(), back_fields.begin(), back_fields.end());
			if (!back_members.empty()) members.insert(members.end(), back_members.begin(), back_members.end());
			if (!back_scores.empty()) scores.insert(scores.end(), back_scores.begin(), back_scores.end());
			bool result = (server.*(info.function))(this);
			keys.clear();
			values.clear();
			fields.clear();
			members.clear();
			scores.clear();
			return result;
		} catch (...) {
			keys.clear();
			values.clear();
			fields.clear();
			members.clear();
			scores.clear();
			throw;
		}
	}
	void server_type::build_api_map()
	{
		//connection API
		api_map["AUTH"].set(&server_type::api_auth).argc(2).type("cs");
		api_map["ECHO"].set(&server_type::api_echo).argc(2).type("cs");
		api_map["PING"].set(&server_type::api_ping);
		api_map["QUIT"].set(&server_type::api_quit);
		api_map["SELECT"].set(&server_type::api_select).argc(2).type("cd");
		//serve API
		//BGREWRITEAOF, BGSAVE, LASTSAVE, SAVE
		//CLIENT KILL, LIST, GETNAME, SETNAME
		//CONFIG GET, SET, RESETSTAT
		//DEBUG OBJECT, SETFAULT
		//SLAVEOF, SYNC
		//INFO, MONITOR, SLOWLOG, 
		api_map["DBSIZE"].set(&server_type::api_dbsize);
		api_map["FLUSHALL"].set(&server_type::api_flushall);
		api_map["FLUSHDB"].set(&server_type::api_flushdb);
		api_map["SHUTDOWN"].set(&server_type::api_shutdown).argc(1,2).type("cs");
		api_map["TIME"].set(&server_type::api_time);
		//transaction API
		api_map["MULTI"].set(&server_type::api_multi);
		api_map["EXEC"].set(&server_type::api_exec);
		api_map["DISCARD"].set(&server_type::api_discard);
		api_map["WATCH"].set(&server_type::api_watch).argc_gte(2).type("ck*");
		api_map["UNWATCH"].set(&server_type::api_unwatch);
		//keys API
		//DUMP, OBJECT
		//MIGRATE, RESTORE
		//SORT
		api_map["KEYS"].set(&server_type::api_keys).argc(2).type("cp");
		api_map["DEL"].set(&server_type::api_del).argc_gte(2).type("ck*");
		api_map["EXISTS"].set(&server_type::api_exists).argc(2).type("ck");
		api_map["EXPIRE"].set(&server_type::api_expire).argc(3).type("ckt");
		api_map["EXPIREAT"].set(&server_type::api_expireat).argc(3).type("ckt");
		api_map["PERSIST"].set(&server_type::api_persist).argc(2).type("ck");
		api_map["TTL"].set(&server_type::api_ttl).argc(2).type("ck");
		api_map["PTTL"].set(&server_type::api_pttl).argc(2).type("ck");
		api_map["MOVE"].set(&server_type::api_move).argc(3).type("ckd");
		api_map["RANDOMKEY"].set(&server_type::api_randomkey);
		api_map["RENAME"].set(&server_type::api_rename).argc(3).type("ckk");
		api_map["RENAMENX"].set(&server_type::api_renamenx).argc(3).type("ckk");
		api_map["TYPE"].set(&server_type::api_type).argc(2).type("ck");
		//strings api
		api_map["GET"].set(&server_type::api_get).argc(2).type("ck");
		api_map["SET"].set(&server_type::api_set).argc(3,8).type("ckvccccc");
		api_map["SETEX"].set(&server_type::api_setex).argc(4).type("cktv");
		api_map["SETNX"].set(&server_type::api_setnx).argc(3).type("ckv");
		api_map["PSETEX"].set(&server_type::api_psetex).argc(4).type("cktv");
		api_map["STRLEN"].set(&server_type::api_strlen).argc(2).type("ck");
		api_map["APPEND"].set(&server_type::api_append).argc(3).type("ckv");
		api_map["GETRANGE"].set(&server_type::api_getrange).argc(4).type("cknn");
		api_map["SUBSTR"].set(&server_type::api_getrange).argc(4).type("cknn");//aka GETRANGE
		api_map["SETRANGE"].set(&server_type::api_setrange).argc(4).type("cknv");
		api_map["GETSET"].set(&server_type::api_getset).argc(3).type("ckv");
		api_map["MGET"].set(&server_type::api_mget).argc_gte(2).type("ck*");
		api_map["MSET"].set(&server_type::api_mset).argc_gte(3).type("ckv**");
		api_map["MSETNX"].set(&server_type::api_msetnx).argc_gte(3).type("ckv**");
		api_map["DECR"].set(&server_type::api_decr).argc(2).type("ck");
		api_map["DECRBY"].set(&server_type::api_decrby).argc(3).type("ckn");
		api_map["INCR"].set(&server_type::api_incr).argc(2).type("ck");
		api_map["INCRBY"].set(&server_type::api_incrby).argc(3).type("ckn");
		api_map["INCRBYFLOAT"].set(&server_type::api_incrbyfloat).argc(3).type("ckn");
		api_map["BITCOUNT"].set(&server_type::api_bitcount).argc(2,4).type("cknn");
		api_map["BITOP"].set(&server_type::api_bitop).argc_gte(4).type("cckk*");
		api_map["GETBIT"].set(&server_type::api_getbit).argc(3).type("ckn");
		api_map["SETBIT"].set(&server_type::api_setbit).argc(4).type("cknv");
		//lists api
		api_map["BLPOP"].set(&server_type::api_blpop).argc_gte(3).type("ck*t");
		api_map["BRPOP"].set(&server_type::api_brpop).argc_gte(3).type("ck*t");
		api_map["BRPOPLPUSH"].set(&server_type::api_brpoplpush).argc(4).type("ckkt");
		api_map["LPUSH"].set(&server_type::api_lpush).argc_gte(3).type("ckv*");
		api_map["RPUSH"].set(&server_type::api_rpush).argc_gte(3).type("ckv*");
		api_map["LPUSHX"].set(&server_type::api_lpushx).argc(3).type("ckv");
		api_map["RPUSHX"].set(&server_type::api_rpushx).argc(3).type("ckv");
		api_map["LPOP"].set(&server_type::api_lpop).argc(2).type("ck");
		api_map["RPOP"].set(&server_type::api_rpop).argc(2).type("ck");
		api_map["LINSERT"].set(&server_type::api_linsert).argc(5).type("ckccv");
		api_map["LINDEX"].set(&server_type::api_lindex).argc(3).type("ckn");
		api_map["LLEN"].set(&server_type::api_llen).argc(2).type("ck");
		api_map["LRANGE"].set(&server_type::api_lrange).argc(4).type("cknn");
		api_map["LREM"].set(&server_type::api_lrem).argc(4).type("cknv");
		api_map["LSET"].set(&server_type::api_lset).argc(4).type("cknv");
		api_map["LTRIM"].set(&server_type::api_ltrim).argc(4).type("cknn");
		api_map["RPOPLPUSH"].set(&server_type::api_rpoplpush).argc(3).type("ckk");
		//hashes api
		api_map["HDEL"].set(&server_type::api_hdel).argc_gte(3).type("cck*");
		api_map["HEXISTS"].set(&server_type::api_hexists).argc(3).type("cck");
		api_map["HGET"].set(&server_type::api_hget).argc(3).type("cck");
		api_map["HGETALL"].set(&server_type::api_hgetall).argc(2).type("cc");
		api_map["HKEYS"].set(&server_type::api_hkeys).argc(2).type("cc");
		api_map["HVALS"].set(&server_type::api_hvals).argc(2).type("cc");
		api_map["HINCRBY"].set(&server_type::api_hincrby).argc(4).type("cckn");
		api_map["HINCRBYFLOAT"].set(&server_type::api_hincrbyfloat).argc(4).type("cckn");
		api_map["HLEN"].set(&server_type::api_hlen).argc(2).type("cc");
		api_map["HMGET"].set(&server_type::api_hmget).argc_gte(3).type("cck*");
		api_map["HMSET"].set(&server_type::api_hmset).argc_gte(4).type("cckv**");
		api_map["HSET"].set(&server_type::api_hset).argc(4).type("cckv");
		api_map["HSETNX"].set(&server_type::api_hsetnx).argc(4).type("cckv");
		//sets api
		api_map["SADD"].set(&server_type::api_sadd).argc_gte(3).type("ckv*");
		api_map["SCARD"].set(&server_type::api_scard).argc(2).type("ck");
		api_map["SISMEMBER"].set(&server_type::api_sismember).argc(3).type("ckv");
		api_map["SMEMBERS"].set(&server_type::api_smembers).argc(2).type("ck");
		api_map["SMOVE"].set(&server_type::api_smove).argc(4).type("ckkv");
		api_map["SPOP"].set(&server_type::api_spop).argc(2).type("ck");
		api_map["SRANDMEMBER"].set(&server_type::api_srandmember).argc_gte(2).type("ckn");
		api_map["SREM"].set(&server_type::api_srem).argc_gte(3).type("ckv*");
		api_map["SDIFF"].set(&server_type::api_sdiff).argc_gte(2).type("ck*");
		api_map["SDIFFSTORE"].set(&server_type::api_sdiffstore).argc_gte(3).type("ckk*");
		api_map["SINTER"].set(&server_type::api_sinter).argc_gte(2).type("ck*");
		api_map["SINTERSTORE"].set(&server_type::api_sinterstore).argc_gte(3).type("ckk*");
		api_map["SUNION"].set(&server_type::api_sunion).argc_gte(2).type("ck*");
		api_map["SUNIONSTORE"].set(&server_type::api_sunionstore).argc_gte(3).type("ckk*");
		//zsets api
		api_map["ZADD"].set(&server_type::api_zadd).argc_gte(4).type("cksm**");
		api_map["ZCARD"].set(&server_type::api_zcard).argc(2).type("ck");
		api_map["ZCOUNT"].set(&server_type::api_zcount).argc(4).type("cknn");
		api_map["ZINCRBY"].set(&server_type::api_zincrby).argc(4).type("cknm");
		api_map["ZINTERSTORE"].set(&server_type::api_zinterstore).argc_gte(4).type("cknv*");//@note タイプが多すぎてパース出来ない
		api_map["ZUNIONSTORE"].set(&server_type::api_zunionstore).argc_gte(4).type("cknv*");//@note タイプが多すぎてパース出来ない
		api_map["ZRANGE"].set(&server_type::api_zrange).argc_gte(4).type("cknnc");
		api_map["ZREVRANGE"].set(&server_type::api_zrevrange).argc_gte(4).type("cknnc");
		api_map["ZRANGEBYSCORE"].set(&server_type::api_zrangebyscore).argc_gte(4).type("cknncccc");
		api_map["ZREVRANGEBYSCORE"].set(&server_type::api_zrevrangebyscore).argc_gte(4).type("cknncccc");
		api_map["ZRANK"].set(&server_type::api_zrank).argc(3).type("ckm");
		api_map["ZREVRANK"].set(&server_type::api_zrevrank).argc(3).type("ckm");
		api_map["ZREM"].set(&server_type::api_zrem).argc_gte(3).type("ckm*");
		api_map["ZREMRANGEBYRANK"].set(&server_type::api_zremrangebyrank).argc(4).type("cknn");
		api_map["ZREMRANGEBYSCORE"].set(&server_type::api_zremrangebyscore).argc(4).type("cknn");
		api_map["ZSCORE"].set(&server_type::api_zscore).argc(3).type("ckm");
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
	database_write_locker::database_write_locker(database_type * database_, client_type * client)
		: database(database_)
		, locker(new rwlock_locker(database_->rwlock, client && client->in_exec() ? no_lock_type : write_lock_type))
	{
	}
	database_read_locker::database_read_locker(database_type * database_, client_type * client)
		: database(database_)
		, locker(new rwlock_locker(database_->rwlock, client && client->in_exec() ? no_lock_type : read_lock_type))
	{
	}
}
