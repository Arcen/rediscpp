#ifndef INCLUDE_REDIS_CPP_MASTER_H
#define INCLUDE_REDIS_CPP_MASTER_H

#include "client.h"

namespace rediscpp
{
	class master_type : public client_type
	{
		friend class server_type;
		enum status {
			waiting_pong_state,
			request_auth_state,
			waiting_auth_state,
			request_replconf_state,
			waiting_replconf_state,
			request_sync_state,
			waiting_sync_state,
			writer_state,
			shutdown_state,
		};
		status state;
		size_t sync_file_size;
		std::string sync_file_path;
		std::shared_ptr<file_type> sync_file;
	public:
		master_type(server_type & server_, std::shared_ptr<socket_type> & client_, const std::string & password_);
		virtual ~master_type();
		virtual void process();
		virtual bool is_master() const { return true; }
	};
};

#endif
