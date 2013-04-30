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
			auto it = api_map.find(command);
			if (it != api_map.end()) {
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
	}
}
