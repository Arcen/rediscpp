#include "master.h"
#include "server.h"
#include "file.h"

namespace rediscpp
{
	master_type::master_type(server_type & server_, std::shared_ptr<socket_type> & client_, const std::string & password_)
		: client_type(server_, client_, password_)
		, state(waiting_pong_state)
		, sync_file_size(0)
	{
	}
	master_type::~master_type()
	{
		sync_file.reset();
		if (!sync_file_path.empty()) {
			::unlink(sync_file_path.c_str());
			sync_file_path.clear();
		}
	}
	void server_type::slaveof(const std::string & host, const std::string & port, bool now)
	{
		if (thread_pool.empty() || now) {
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
				connection->set_callback(client_callback);
				connection->set_nonblocking();
				if (!connection->connect(address)) {
					lprintf(__FILE__, __LINE__, error_level, "failed to connect master");
					return;
				}
				std::shared_ptr<client_type> client(new master_type(*this, connection, password));
				client->set(client);
				connection->set_extra2(client.get());
				//@note 送信バッファに設定することで、接続済みのイベントを取得して、送信する
				std::string ping("*1\r\n$4\r\nPING\r\n");
				connection->send(ping.c_str(), ping.size());
				append_client(client);
			}
		} else {
			std::shared_ptr<job_type> job(new job_type(job_type::slaveof_type));
			job->arg1 = host;
			job->arg2 = port;
			jobs.push(job);
			event->send();
		}
	}
	void master_type::process()
	{
		if (state == writer_state) {
			client_type::process();
			return;
		}
		if (events & EPOLLIN) {//recv
			client->recv();
		}
		if (events & EPOLLOUT) {//send
			client->send();
		}
		while (true) {
			switch (state) {
			case waiting_pong_state:
				if (client->should_recv()) {
					std::string line;
					if (parse_line(line)) {
						if (line == "+PONG") {
							state = request_replconf_state;
							continue;
						} else if (line.substr(0, 7) == "-NOAUTH") {
							state = request_auth_state;
							continue;
						} else {
							lprintf(__FILE__, __LINE__, error_level, "failed to get ping response");
							state = shutdown_state;
							continue;
						}
					}
				}
				return;
			case request_auth_state:
				{
					std::string auth = format("*2\r\n$4\r\nAUTH\r\n$%d\r\n", password.size()) + password + "\r\n";
					if (!client->send(auth.c_str(), auth.size())) {
						lprintf(__FILE__, __LINE__, error_level, "failed to send auth");
						state = shutdown_state;
						continue;
					}
					client->send();
					state = waiting_auth_state;
				}
				return;
			case waiting_auth_state:
				if (client->should_recv()) {
					std::string line;
					if (parse_line(line)) {
						if (line.substr(0,1) == "+") {
							state = request_replconf_state;
							continue;
						} else {
							lprintf(__FILE__, __LINE__, error_level, "failed to get auth response");
							state = shutdown_state;
							continue;
						}
					}
				}
				return;
			case request_replconf_state:
				{
					std::string port = format("%d", server.listening_port);
					std::string replconf = format("*3\r\n$8\r\nREPLCONF\r\n$14\r\nlistening-port\r\n$%zd\r\n%s\r\n", port.size(), port.c_str());
					if (!client->send(replconf.c_str(), replconf.size())) {
						lprintf(__FILE__, __LINE__, error_level, "failed to send replconf");
						state = shutdown_state;
						continue;
					}
					client->send();
					state = waiting_replconf_state;
				}
				return;
			case waiting_replconf_state:
				if (client->should_recv()) {
					std::string line;
					if (parse_line(line)) {
						if (line.substr(0,1) == "+") {
							state = request_sync_state;
							continue;
						} else {
							lprintf(__FILE__, __LINE__, error_level, "failed to get replconf response");
							state = shutdown_state;
							continue;
						}
					}
				}
				return;
			case request_sync_state:
				{
					std::string sync("*1\r\n$4\r\nSYNC\r\n");
					if (!client->send(sync.c_str(), sync.size())) {
						lprintf(__FILE__, __LINE__, error_level, "failed to send sync");
						state = shutdown_state;
						continue;
					}
					client->send();
					state = waiting_sync_state;
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
							char path[] = "/tmp/redis.sync.rdb.XXXXXX";
							int fd = mkstemp(path);
							if (fd < 0) {
								lprintf(__FILE__, __LINE__, error_level, "failed to create tmp file");
								state = shutdown_state;
								continue;
							}
							close(fd);
							sync_file_path = path;
							sync_file = file_type::create(sync_file_path);
						}
						continue;
					}
				} else if (sync_file) {
					if (client->should_recv()) {
						auto & buf = client->get_recv();
						size_t size = std::min(sync_file_size, buf.size());
						auto end = buf.begin() + size;
						std::vector<uint8_t> tmp(buf.begin(), end);
						sync_file->write(tmp);
						sync_file->flush();
						buf.erase(buf.begin(), end);
						sync_file_size -= size;
					}
					if (sync_file_size == 0) {
						sync_file.reset();
						//load
						if (!server.load(sync_file_path)) {
							lprintf(__FILE__, __LINE__, error_level, "failed to load sync file");
							state = shutdown_state;
							continue;
						}
						::unlink(sync_file_path.c_str());
						sync_file_path.clear();
						state = writer_state;
						lprintf(__FILE__, __LINE__, debug_level, "now waiting master request");
						continue;
					}
				} else {
					lprintf(__FILE__, __LINE__, error_level, "failed to get sync file object");
					state = shutdown_state;
					continue;
				}
				return;
			case writer_state:
				client_type::process();
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
