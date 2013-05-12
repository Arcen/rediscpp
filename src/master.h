#ifndef INCLUDE_REDIS_CPP_MASTER_H
#define INCLUDE_REDIS_CPP_MASTER_H

#include "network.h"
#include "thread.h"
#include "file.h"

namespace rediscpp
{
	class server_type;
	struct api_info;
	class master_type
	{
		friend class server_type;
		server_type & server;
		std::shared_ptr<socket_type> client;
		std::string password;
		timeval_type current_time;
		int events;//for thread
		std::weak_ptr<master_type> self;
		enum status {
			waiting_pong_state,
			request_auth_state,
			waiting_auth_state,
			request_replconf_state,
			waiting_replconf_state,
			request_sync_state,
			waiting_sync_state,
			shutdown_state,
		};
		status state;
		size_t sync_file_size;
		std::shared_ptr<file_type> sync_file;
	public:
		master_type(server_type & server_, std::shared_ptr<socket_type> & client_, const std::string & password_);
		void close_after_send() { client->close_after_send(); }
		timeval_type get_time() const { return current_time; }
		void process();
		void set(std::shared_ptr<master_type> self_) { self = self_; }
		std::shared_ptr<master_type> get() { return self.lock(); }
	private:
		bool parse_line(std::string & line);
	};
};

#endif
