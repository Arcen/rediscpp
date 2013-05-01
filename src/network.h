#ifndef INCLUDE_REDIS_CPP_NETWORK_H
#define INCLUDE_REDIS_CPP_NETWORK_H

#include <stdint.h>
#include <sys/socket.h> 
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/uio.h>

#include "common.h"

namespace rediscpp
{
	class address_type
	{
		union sockaddr_any
		{
			sockaddr_un un;
			sockaddr_in in;
			sockaddr_in6 in6;
			sockaddr_any();
		};
		sockaddr_any addr;
	public:
		address_type();
		bool set_hostname(const std::string & hostname);
		bool set_port(uint16_t port);
		sa_family_t get_family() const;
		void set_family(sa_family_t family);
		sockaddr * get_sockaddr();
		const sockaddr * get_sockaddr() const;
		size_t get_sockaddr_size() const;
	};

	class poll_type;
	class socket_type
	{
		int s;
		std::shared_ptr<address_type> local;
		//std::shared_ptr<address_type> peer;///<リモートアドレス
		std::weak_ptr<socket_type> self;
		std::weak_ptr<poll_type> poll;
		bool finished_to_read;
		bool finished_to_write;
		int shutdowning;
		void * extra;
		bool broken;
		friend class poll_type;
		epoll_event event;
		socket_type();
		socket_type(int s);
	public:
		~socket_type();
		void close();
		bool shutdown(bool reading, bool writing);
		static std::shared_ptr<socket_type> create(const address_type & address, bool stream = true);
		bool set_nonblocking(bool nonblocking = true);
		bool set_reuse(bool reuse = true);
		bool set_nodelay(bool nodelay = true);
		bool bind(std::shared_ptr<address_type> address);
		bool listen(int queue_count);
		std::shared_ptr<socket_type> accept();
		int get_handle() const { return s; }
		bool is_broken() const { return broken; }
	private:
		std::deque<uint8_t> recv_buffer;
		std::deque<std::pair<std::vector<uint8_t>,size_t>> send_buffers;
		std::vector<iovec> send_vectors;
		std::function<void(socket_type * s,int)> on_event_function;
	public:
		bool should_send() const { return ! send_buffers.empty() && ! is_write_shutdowned(); }
		bool should_recv() const { return ! recv_buffer.empty() && ! is_read_shutdowned(); }
		void set_extra(void * extra_) { extra = extra_; }
		void * get_extra() { return extra; }
		void set_callback(std::function<void(socket_type * s,int)> function);
		void on_event(int flag);
		void set_poll(std::shared_ptr<poll_type> poll);
		bool send();
		bool recv();
		bool send(const void * buf, size_t len);
		std::deque<uint8_t> & get_recv() { return recv_buffer; }
		bool recv_done() const { return finished_to_read; }
		std::shared_ptr<socket_type> get() { return self.lock(); }
		void close_after_send();
		bool is_read_shutdowned() const { return shutdowning == SHUT_RD || shutdowning == SHUT_RDWR; }
		bool is_write_shutdowned() const { return shutdowning == SHUT_WR || shutdowning == SHUT_RDWR; }
	};

	class poll_type
	{
		int fd;
		int count;
		std::vector<epoll_event> events;///<受信用
		std::weak_ptr<poll_type> self;
		poll_type();
	public:
		static std::shared_ptr<poll_type> create();
		~poll_type();
		void close();
	private:
		bool operation(std::shared_ptr<socket_type> socket, int op);
	public:
		bool append(std::shared_ptr<socket_type> socket) { return operation(socket, EPOLL_CTL_ADD); }
		bool modify(std::shared_ptr<socket_type> socket) { return operation(socket, EPOLL_CTL_MOD); }
		bool remove(std::shared_ptr<socket_type> socket) { return operation(socket, EPOLL_CTL_DEL); }
		bool wait(int timeout_milli_sec = 0);
		int get_count() const { return count; }
	};
}

#endif
