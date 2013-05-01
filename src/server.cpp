#include "server.h"
#include "log.h"
#include <algorithm>
#include <ctype.h>

namespace rediscpp
{
	server_type::server_type()
		: shutdown(false)
	{
		databases.resize(1);
		build_api_map();
	}
	client_type::client_type(std::shared_ptr<socket_type> & client_, const std::string & password_)
		: client(client_)
		, argument_count(0)
		, argument_index(0)
		, argument_size(argument_is_undefined)
		, password(password_)
		, db_index(0)
		, transaction(false)
	{
		write_cache.reserve(1500);
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
		listening->set_nonblocking();
		poll = poll_type::create(1024);
		poll->append(listening);
		while (true) {
			try {
				poll->wait(-1);
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
		if (events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
			//lputs(__FILE__, __LINE__, info_level, "client closed");
			s->close();
			clients.erase(s);
			return;
		}
		if (events & EPOLLIN) {//recv
			//lputs(__FILE__, __LINE__, info_level, "client EPOLLIN");
			s->recv();
			if (s->should_recv()) {
				clients[s]->parse(this);
			}
			if (s->recv_done() && !s->should_send()) {
				//lputs(__FILE__, __LINE__, info_level, "client closed");
				s->close();
				clients.erase(s);
				return;
			}
		}
		if (events & EPOLLOUT) {//send
			lputs(__FILE__, __LINE__, info_level, "client EPOLLOUT");
			s->send();
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
			//lputs(__FILE__, __LINE__, info_level, "client connected");
			client->set_callback(client_event);
			client->set_nonblocking();
			client->set_extra(this);
			client->set_nodelay();
			poll->append(client);
			client_type * ct = new client_type(client, password);
			clients[client.get()].reset(ct);
		} else {
			lprintf(__FILE__, __LINE__, info_level, "other events %x", events);
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
	bool client_type::parse(server_type * server)
	{
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
					if (!server->execute(this)) {
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
				if (!server->execute(this)) {
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
			auto it = api_map.find(command);
			if (it != api_map.end()) {
				auto func = it->second;
				return ((this)->*func)(client);
			}
			//lprintf(__FILE__, __LINE__, info_level, "not supported command %s", command.c_str());
		} catch (std::exception & e) {
			client->response_error(e.what());
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
		api_map["AUTH"] = &server_type::api_auth;
		api_map["ECHO"] = &server_type::api_echo;
		api_map["PING"] = &server_type::api_ping;
		api_map["QUIT"] = &server_type::api_quit;
		api_map["SELECT"] = &server_type::api_select;
		//serve API
		api_map["DBSIZE"] = &server_type::api_dbsize;
		api_map["FLUSHALL"] = &server_type::api_flushall;
		api_map["FLUSHDB"] = &server_type::api_flushdb;
		api_map["SHUTDOWN"] = &server_type::api_shutdown;
		api_map["TIME"] = &server_type::api_time;
		//transaction API
		api_map["MULTI"] = &server_type::api_multi;
		api_map["EXEC"] = &server_type::api_exec;
		api_map["DISCARD"] = &server_type::api_discard;
		api_map["WATCH"] = &server_type::api_watch;
		api_map["UNWATCH"] = &server_type::api_unwatch;
		//keys API
		api_map["DEL"] = &server_type::api_del;
		api_map["EXISTS"] = &server_type::api_exists;
		api_map["EXPIRE"] = &server_type::api_expire;
		api_map["EXPIREAT"] = &server_type::api_expireat;
		api_map["PERSIST"] = &server_type::api_persist;
		api_map["TTL"] = &server_type::api_ttl;
		api_map["PTTL"] = &server_type::api_pttl;
		api_map["MOVE"] = &server_type::api_move;
		api_map["RANDOMKEY"] = &server_type::api_randomkey;
		api_map["RENAME"] = &server_type::api_rename;
		api_map["RENAMENX"] = &server_type::api_renamenx;
		api_map["TYPE"] = &server_type::api_type;
		//strings api
		api_map["GET"] = &server_type::api_get;
		api_map["SET"] = &server_type::api_set;
		api_map["SETEX"] = &server_type::api_setex;
		api_map["SETNX"] = &server_type::api_setnx;
		api_map["PSETEX"] = &server_type::api_psetex;
		api_map["STRLEN"] = &server_type::api_strlen;
	}
}
