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
		std::shared_ptr<address_type> peer;
		std::weak_ptr<socket_type> self;
		std::weak_ptr<poll_type> poll;
		bool finished_to_read;
		socket_type();
		socket_type(int s);
	public:
		~socket_type();
		void close();
		static std::shared_ptr<socket_type> create(const address_type & address, bool stream = true);
		bool set_blocking(bool blocking = true);
		bool set_reuse(bool reuse = true);
		bool bind(std::shared_ptr<address_type> address);
		bool listen(int queue_count);
		std::shared_ptr<socket_type> accept();
		int get_handle() const { return s; }
	private:
		void * extra;
		std::vector<uint8_t> recv_buffer;
		std::deque<std::pair<std::vector<uint8_t>,size_t>> send_buffers;
		std::vector<iovec> send_vectors;
		std::function<void(socket_type * s,int)> on_event_function;
	public:
		bool should_send() const { return ! send_buffers.empty(); }
		bool should_recv() const { return ! recv_buffer.empty(); }
		void set_extra(void * extra_) { extra = extra_; }
		void * get_extra() { return extra; }
		void set_callback(std::function<void(socket_type * s,int)> function);
		void on_event(int flag);
		void set_poll(std::shared_ptr<poll_type> poll);
		bool send(const void * buf, size_t len);
		bool send();
		bool recv();
	};

	class poll_type
	{
		int fd;
		int count;
		std::vector<epoll_event> events;///<��M�p
		std::weak_ptr<poll_type> self;
		poll_type(size_t capacity);
	public:
		static std::shared_ptr<poll_type> create(size_t capacity);
		~poll_type();
		void close();
	private:
		bool operation(std::shared_ptr<socket_type> socket, int op);
	public:
		bool append(std::shared_ptr<socket_type> socket) { return operation(socket, EPOLL_CTL_ADD); }
		bool modify(std::shared_ptr<socket_type> socket) { return operation(socket, EPOLL_CTL_MOD); }
		bool remove(std::shared_ptr<socket_type> socket) { return operation(socket, EPOLL_CTL_DEL); }
		bool wait(int timeout_milli_sec = 0);
	};
}

#endif