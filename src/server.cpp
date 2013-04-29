#include "server.h"

namespace rediscpp
{
	bool server_type::start(const std::string & hostname, const std::string & port)
	{
		std::shared_ptr<address_type> addr(new address_type);
		addr->set_hostname(hostname.c_str());
		addr->set_port(atoi(port.c_str()));
		listening = socket_type::create(*addr);
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
			s->recv();
			if (s->should_recv()) {
				//ここで、コマンドのパースを行う
			}
		}
		if (events & EPOLLOUT) {//send
			s->send();
		}
	}
	void server_type::on_server_event(socket_type * s, int events)
	{
		std::shared_ptr<socket_type> client = s->accept();
		if (client.get()) {
			client->set_callback(client_event);
			client->set_blocking(false);
			client->set_extra(this);
			poll->append(client);
			clients.push_back(client);
		}
	}
}
