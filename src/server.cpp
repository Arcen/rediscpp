#include "server.h"
#include "log.h"
#include <algorithm>
#include <ctype.h>

namespace rediscpp
{
	server_type::server_type()
	{
		build_function_map();
	}
	void server_type::build_function_map()
	{
		function_map["PING"] = &server_type::function_ping;
		function_map["QUIT"] = &server_type::function_quit;
		function_map["ECHO"] = &server_type::function_echo;
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
			lputs(__FILE__, __LINE__, info_level, "client connected");
			client->set_callback(client_event);
			client->set_blocking(false);
			client->set_extra(this);
			poll->append(client);
			clients[client.get()].reset(new client_type(client));
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
							arguments.push_back(std::make_pair<std::string,bool>(std::string(), false));
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
					arguments.push_back(std::make_pair<std::string,bool>(arg_data, true));
					argument_size = argument_is_undefined;
					++argument_index;
				}
			} else {
				if (!server->execute(this)) {
					response_status("ERR unknown");
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
		std::string response = "-" + state + "\r\n";
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
	void client_type::response_bulk(const std::string & bulk)
	{
		std::string response = format("$%d\r\n", bulk.size());
		client->send(response.c_str(), response.size());
		client->send(bulk.c_str(), bulk.size());
		response = "\r\n";
		client->send(response.c_str(), response.size());
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
		auto & arguments = client->get_arguments();
		if (arguments.empty()) {
			return false;
		}
		auto command = arguments.front().first;
		arguments.pop_front();
		std::transform(command.begin(), command.end(), command.begin(), toupper);
		auto it = function_map.find(command);
		if (it != function_map.end()) {
			auto func = it->second;
			return ((this)->*func)(client);
		}
		return false;
	}
	bool server_type::function_ping(client_type * client)
	{
		client->response_status("PONG");
		return true;
	}
	bool server_type::function_quit(client_type * client)
	{
		client->response_status("OK");
		client->close_after_send();
		return true;
	}
	bool server_type::function_echo(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.empty()) {
			return false;
		}
		auto bulk = arguments.front().first;
		client->response_bulk(bulk);
		return true;
	}
}
