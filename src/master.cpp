#include "master.h"
#include "server.h"
#include "client.h"

namespace rediscpp
{
	master_type::master_type(server_type & server_, std::shared_ptr<socket_type> & client_, const std::string & password_)
		: server(server_)
		, client(client_)
		, password(password_)
		, current_time(0, 0)
		, events(0)
		, state(waiting_pong_state)
		, sync_file_size(0)
	{
	}
	void server_type::slaveof(const std::string & host, const std::string & port, bool now)
	{
		if (thread_pool.empty() || now) {
			lprintf(__FILE__, __LINE__, debug_level, "slaveof now");
			if (master) {
				if (host == "no" && port == "one") {
					lprintf(__FILE__, __LINE__, debug_level, "shutdown master now");
					//@todo もう少し安全な停止を考える 
					master->client->shutdown(true, true);
				} else {
					//@todo 既にslaveの場合、slaveになる必要があるので、何か処理を考える必要がある
					lprintf(__FILE__, __LINE__, debug_level, "could not shutdown master now");
				}
			} else {
				lprintf(__FILE__, __LINE__, debug_level, "connecting master now");
				std::shared_ptr<address_type> address(new address_type());
				if (!address->set_hostname(host)) {
					lprintf(__FILE__, __LINE__, error_level, "failed to set master hostname : %s", host.c_str());
					return;
				}
				bool is_valid = false;
				uint16_t port_ = atou16(port,is_valid);
				if (!is_valid || !address->set_port(port_)) {
					lprintf(__FILE__, __LINE__, error_level, "failed to set master port : %s", port.c_str());
					return;
				}
				std::shared_ptr<socket_type> connection = socket_type::create(*address);
				connection->set_extra(this);
				connection->set_callback(master_callback);
				connection->set_nonblocking();
				if (!connection->connect(address)) {
					lprintf(__FILE__, __LINE__, error_level, "failed to connect master");
					return;
				}
				lprintf(__FILE__, __LINE__, debug_level, "send ping");
				std::string ping("*1\r\n$4\r\nPING\r\n");
				connection->send(ping.c_str(), ping.size());
				master.reset(new master_type(*this, connection, password));
				poll->append(connection);
			}
		} else {
			lprintf(__FILE__, __LINE__, debug_level, "add job slaveof");
			std::shared_ptr<job_type> job(new job_type(job_type::slaveof_type));
			job->arg1 = host;
			job->arg2 = port;
			jobs.push(job);
			event->send();
		}
	}
	void server_type::on_master(int events)
	{
		if (!master) {
			return;
		}
		if ((events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) || master->client->is_broken()) {
			lprintf(__FILE__, __LINE__, debug_level, "remove master");
			remove_master();
			return;
		}
		master->events = events;
		master->process();
		if (master->client->done()) {
			lprintf(__FILE__, __LINE__, debug_level, "remove master");
			remove_master();
		} else {
			master->client->mod();
		}
	}
	bool master_type::parse_line(std::string & line)
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
	void master_type::process()
	{
		if (events & EPOLLIN) {//recv
			//lputs(__FILE__, __LINE__, info_level, "client EPOLLIN");
			client->recv();
		} else if (events & EPOLLOUT) {//send
			client->send();
		}
		while (true) {
			switch (state) {
			case waiting_pong_state:
				if (client->should_recv()) {
					std::string line;
					if (parse_line(line)) {
						if (line == "+PONG") {
							std::string sync("*1\r\n$4\r\nSYNC\r\n");
							if (!client->send(sync.c_str(), sync.size())) {
								lprintf(__FILE__, __LINE__, error_level, "failed to send sync");
								state = shutdown_state;
								continue;
							}
							client->send();
							state = waiting_sync_state;
							continue;
						} else if (line.substr(0, 7) == "-NOAUTH") {
							std::string auth = format("*2\r\n$4\r\nAUTH\r\n$%d\r\n", password.size()) + password + "\r\n";
							if (!client->send(auth.c_str(), auth.size())) {
								lprintf(__FILE__, __LINE__, error_level, "failed to send auth");
								state = shutdown_state;
								continue;
							}
							client->send();
							state = waiting_auth_state;
						} else {
							lprintf(__FILE__, __LINE__, error_level, "failed to get ping response");
							state = shutdown_state;
							continue;
						}
					}
				}
				return;
			case waiting_auth_state:
				if (client->should_recv()) {
					std::string line;
					if (parse_line(line)) {
						if (line.substr(0,1) == "+") {
							std::string sync("*1\r\n$4\r\nSYNC\r\n");
							if (!client->send(sync.c_str(), sync.size())) {
								lprintf(__FILE__, __LINE__, error_level, "failed to send sync");
								state = shutdown_state;
								continue;
							}
							client->send();
							state = waiting_sync_state;
						} else {
							lprintf(__FILE__, __LINE__, error_level, "failed to get auth response");
							state = shutdown_state;
							continue;
						}
					}
				}
				return;
			case waiting_sync_state:
				if (!sync_file_size) {
					std::string line;
					if (parse_line(line)) {
						bool is_valid = false;
						if (line.substr(0,1) == "$") {
							sync_file_size = atoi64(line.substr(1), is_valid);
						}
						if (!sync_file_size || !is_valid) {
							lprintf(__FILE__, __LINE__, error_level, "failed to get sync file size");
							state = shutdown_state;
						} else {
							//@todo テンポラリファイルにする必要がある
							sync_file = file_type::create("/tmp/redis.sync.rdb");
						}
						continue;
					}
				} else if (sync_file) {
					if (client->should_recv()) {
						lprintf(__FILE__, __LINE__, error_level, "recving sync file");
						auto & buf = client->get_recv();
						size_t size = std::min(sync_file_size, buf.size());
						auto end = buf.begin() + size;
						std::vector<uint8_t> tmp(buf.begin(), end);
						sync_file->write(tmp);
						sync_file->flush();
						buf.erase(buf.begin(), end);
						sync_file_size -= size;
						if (sync_file_size == 0) {
							sync_file.reset();
							//load
							if (!server.load("/tmp/redis.sync.rdb")) {
								lprintf(__FILE__, __LINE__, error_level, "failed to load sync file");
								state = shutdown_state;
								continue;
							}
							::unlink("/tmp/redis.sync.rdb");
							client->set_callback(server_type::client_callback);
							std::shared_ptr<client_type> master_client(new client_type(server, client, ""));
							master_client->set(master_client);
							client->set_extra2(master_client.get());
							master_client->set_master();//マスターとして取り扱う
							server.append_client(master_client);
							return;
						}
					}
				} else {
					lprintf(__FILE__, __LINE__, error_level, "failed to get sync file object");
					state = shutdown_state;
					continue;
				}
				return;
			case shutdown_state:
			default:
				lprintf(__FILE__, __LINE__, error_level, "master shutdown now");
				client->shutdown(true, true);
				return;
			}
		}
	}
};
