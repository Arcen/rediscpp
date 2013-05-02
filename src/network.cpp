#include "network.h"
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <limits.h>
#include <algorithm>

namespace rediscpp
{
	address_type::sockaddr_any::sockaddr_any()
	{
		memset(this, 0, sizeof(*this));
	}
	address_type::address_type()
	{
	}
	bool address_type::set_hostname(const std::string & hostname)
	{
		if (hostname.empty()) {
			return false;
		}
		if (*hostname.begin() == '/') {
			if (hostname.length() < sizeof(addr.un.sun_path)) {
				addr.un.sun_family = AF_UNIX;
				strcpy(addr.un.sun_path, hostname.c_str());
				return true;
			}
			return false;
		}
		uint16_t families[2] = { AF_INET, AF_INET6 };
		for (int i = 0; i < 2; ++i) {
			addrinfo hints;
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = families[i];
			hints.ai_socktype = SOCK_STREAM;
			addrinfo *res = 0;
			int r = getaddrinfo(hostname.c_str(), 0, &hints, &res);
			std::shared_ptr<addrinfo> keeper;
			if (res) keeper.reset(res, freeaddrinfo);
			if (r || !res) {
				if (r == EAI_AGAIN || (r == EAI_SYSTEM && errno == EINTR)) {
					keeper.reset();
					res = 0;
					r = getaddrinfo(hostname.c_str(), 0, &hints, &res);
					if (res) keeper.reset(res, freeaddrinfo);
					if (r || !res) return false;
				} else {
					return false;
				}
			}
			for (addrinfo * ai = res; ai != NULL; ai = ai->ai_next) {
				if (ai->ai_family == AF_INET && families[i] == AF_INET) {
					addr.in.sin_family = AF_INET;
					addr.in.sin_addr = reinterpret_cast<sockaddr_in*>( ai->ai_addr )->sin_addr;
					return true;
				} else if (ai->ai_family == AF_INET6 && families[i] == AF_INET6) {
					addr.in6.sin6_family = AF_INET6;
					addr.in6.sin6_addr = reinterpret_cast<sockaddr_in6*>( ai->ai_addr )->sin6_addr;
					return true;
				}
			}
		}
		return false;
	}
	bool address_type::set_port(uint16_t port)
	{
		if (addr.in.sin_family == AF_INET) {
			addr.in.sin_port = htons(port);
			return true;
		}
		if (addr.in6.sin6_family == AF_INET6) {
			addr.in6.sin6_port = htons(port);
			return true;
		}
		return false;
	}
	sa_family_t address_type::get_family() const
	{
		if (addr.un.sun_family == AF_UNIX) {
			return AF_UNIX;
		}
		if (addr.in.sin_family == AF_INET) {
			return AF_INET;
		}
		if (addr.in6.sin6_family == AF_INET6) {
			return AF_INET6;
		}
		return 0;
	}
	void address_type::set_family(sa_family_t family)
	{
		if (family == AF_UNIX) {
			addr.un.sun_family = AF_UNIX;
		}
		if (family == AF_INET) {
			addr.in.sin_family = AF_INET;
		}
		if (family == AF_INET6) {
			addr.in6.sin6_family = AF_INET6;
		}
	}
	const sockaddr * address_type::get_sockaddr() const
	{
		return reinterpret_cast<const sockaddr *>(&addr);
	}
	sockaddr * address_type::get_sockaddr()
	{
		return reinterpret_cast<sockaddr *>(&addr);
	}
	size_t address_type::get_sockaddr_size() const
	{
		if (addr.un.sun_family == AF_UNIX) {
			return sizeof(addr.un);
		}
		if (addr.in.sin_family == AF_INET) {
			return sizeof(addr.in);
		}
		if (addr.in6.sin6_family == AF_INET6) {
			return sizeof(addr.in6);
		}
		return 0;
	}
	socket_type::socket_type(int fd_)
		: pollable_type(fd_)
		, finished_to_read(false)
		, finished_to_write(false)
		, shutdowning(-1)
		, broken(false)
	{
	}
	socket_type::~socket_type()
	{
		close();
	}
	std::shared_ptr<socket_type> socket_type::create(const address_type & address, bool stream)
	{
		int fd = ::socket(address.get_family(), stream ? SOCK_STREAM : SOCK_DGRAM, 0);
		if (fd < 0) {
			lputs(__FILE__, __LINE__, error_level, "::socket failed : " + string_error(errno));
			return std::shared_ptr<socket_type>();
		}
		std::shared_ptr<socket_type> result = std::shared_ptr<socket_type>(new socket_type(fd));
		result->self = result;
		return result;
	}
	bool socket_type::set_nonblocking(bool nonblocking)
	{
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags == -1) {
			lputs(__FILE__, __LINE__, error_level, "::fcntl(F_GETFL) failed : " + string_error(errno));
			return false;
		}
		if (((flags & O_NONBLOCK) != 0) != nonblocking) {
			flags ^= O_NONBLOCK;
			int r = fcntl(fd, F_SETFL, flags);
			if (r < 0) {
				lputs(__FILE__, __LINE__, error_level, "::fcntl(F_SETFL) failed : " + string_error(errno));
				return false;
			}
		}
		return true;
	}
	bool socket_type::set_reuse(bool reuse)
	{
		int option = reuse ? 1 : 0;
		int r = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
		if (r < 0) {
			lputs(__FILE__, __LINE__, error_level, "::setsockopt(SO_REUSEADDR) failed : " + string_error(errno));
			return false;
		}
		return true;
	}
	bool socket_type::set_nodelay(bool nodelay)
	{
		int option = nodelay ? 1 : 0;
		int r = setsockopt(fd, SOL_TCP, TCP_NODELAY, &option, sizeof(option));
		if (r < 0) {
			lputs(__FILE__, __LINE__, error_level, "::setsockopt(TCP_NODELAY) failed : " + string_error(errno));
			return false;
		}
		return true;
	}
	bool socket_type::bind(std::shared_ptr<address_type> address)
	{
		int r = ::bind(fd, address->get_sockaddr(), address->get_sockaddr_size());
		if (r < 0) {
			lputs(__FILE__, __LINE__, error_level, "::bind failed : " + string_error(errno));
			return false;
		}
		local = address;
		return true;
	}
	bool socket_type::listen(int queue_count)
	{
		int r = ::listen(fd, queue_count);
		if (r < 0) {
			lputs(__FILE__, __LINE__, error_level, "::listen failed : " + string_error(errno));
			return false;
		}
		return true;
	}
	std::shared_ptr<socket_type> socket_type::accept()
	{
		std::shared_ptr<address_type> addr(new address_type());
		addr->set_family(local->get_family());
		socklen_t addr_len = static_cast<socklen_t>(addr->get_sockaddr_size());
		int cs = ::accept(fd, addr->get_sockaddr(), &addr_len);
		if (cs < 0) {
			return std::shared_ptr<socket_type>();
		}
		std::shared_ptr<socket_type> child(new socket_type(cs));
		child->self = child;
		//child->peer = addr;
		return child;
	}
	bool socket_type::send(const void * buf, size_t len)
	{
		if (len == 0) {
			return true;
		}
		send_buffers.push_back(std::make_pair<std::vector<uint8_t>, size_t>(std::vector<uint8_t>(), 0));
		std::vector<uint8_t> & last = send_buffers.back().first;
		last.assign(reinterpret_cast<const uint8_t*>(buf), reinterpret_cast<const uint8_t*>(buf) + len);
		return true;
	}
	bool socket_type::shutdown(bool reading, bool writing)
	{
		if (is_read_shutdowned()) {
			reading = true;
		}
		if (is_write_shutdowned()) {
			writing = true;
		}
		int shut = (reading) ? (writing ? SHUT_RDWR : SHUT_RD) : (writing ? SHUT_WR : -1);
		if (shut == shutdowning) {
			return true;
		}
		shutdowning = shut;
		int r = ::shutdown(fd, shutdowning);
		if (r < 0) {
			switch (errno) {
			case EBADF:
			case ENOTCONN:
				return true;
			}
			lprintf(__FILE__, __LINE__, error_level, "::shutdown failed : %s", string_error(errno).c_str());
			return false;
		}
		return true;
	}
	bool socket_type::send()
	{
		if (fd < 0) {
			lprintf(__FILE__, __LINE__, error_level, "send(%d) closed", fd);
			return false;
		}
		if (!send_buffers.empty()) {
			send_vectors.resize(std::min<size_t>(IOV_MAX, send_buffers.size()));
			for (size_t i = 0, n = send_buffers.size(); i < n; ++i) {
				auto & src = send_buffers[i];
				iovec & iv = send_vectors[i];
				iv.iov_base = & src.first[0] + src.second;
				iv.iov_len = src.first.size() - src.second;
			}
			ssize_t r = ::writev(fd, &send_vectors[0], static_cast<int>(send_vectors.size()));
			if (r < 0) {
				if (errno == EAGAIN) {
					return false;
				}
				broken = true;
				lprintf(__FILE__, __LINE__, error_level, "writev(%d) failed:%s", fd, string_error(errno).c_str());
				return false;
			}
			while (0 < r && ! send_buffers.empty()) {
				auto & front_buffer = send_buffers.front();
				auto & buf = front_buffer.first;
				auto & offset = front_buffer.second;
				if (r < buf.size() - offset) {
					offset += r;
					r = 0;
					break;
				} else {
					r -= buf.size() - offset;
					send_buffers.pop_front();
				}
			}
		}
		if (send_buffers.empty()) {
			if (finished_to_write) {
				shutdown(false, true);
				return true;
			}
		}
		mod();
		return true;
	}
	bool socket_type::recv()
	{
		if (finished_to_read) {
			return true;
		}
		uint8_t buf[1500];
		while (true) {
			ssize_t r = ::read(fd, buf, sizeof(buf));
			if (0 < r) {
				recv_buffer.insert(recv_buffer.end(), &buf[0], &buf[0] + r);
			} else if (r == 0) {
				finished_to_read = true;
				break;
			} else {
				break;
			}
		}
		return true;
	}
	void socket_type::close_after_send()
	{
		finished_to_write = true;
		send();
	}
	void pollable_type::close()
	{
		if (0 <= fd) {
			auto poll = this->poll.lock();
			if (poll.get()) {
				auto self = this->self.lock();
				poll->remove(self);
			}
			::close(fd);
			fd = -1;
		}
	}
	void pollable_type::mod()
	{
		auto poll_ = poll.lock();
		if (poll_.get()) {
			poll_->modify(self.lock());
		} else {
			lprintf(__FILE__, __LINE__, error_level, "not added, could not mod");
		}
	}
	std::shared_ptr<poll_type> poll_type::create()
	{
		std::shared_ptr<poll_type> poll(new poll_type());
		poll->self = poll;
		return poll;
	}
	poll_type::poll_type()
		: fd(-1)
		, count(0)
		, events(1024)
	{
		fd = ::epoll_create1(EPOLL_CLOEXEC);
		if (fd < 0) {
			throw std::runtime_error(std::string("poll_type::epoll_create failed:") + string_error(errno));
		}
	}
	poll_type::~poll_type()
	{
		close();
	}
	void poll_type::close()
	{
		if (0 <= fd) {
			::close(fd);
			fd = -1;
		}
	}
	///@param[in] op EPOLL_CTL_ADD, EPOLL_CTL_MOD, EPOLL_CTL_DEL
	bool poll_type::operation(std::shared_ptr<pollable_type> pollable, int op)
	{
		if (!pollable) {
			lprintf(__FILE__, __LINE__, error_level, "empty object");
			return false;
		}
		auto & events = pollable->events;
		auto newevents = pollable->get_events();
		/*
		if (newevents == events.events && op == EPOLL_CTL_MOD && !(newevents & EPOLLONESHOT)) {
			lprintf(__FILE__, __LINE__, error_level, "no need modify");
			return true;
		}
		*/
		events.events = newevents;
		int r = epoll_ctl(fd, op, pollable->get_handle(), op == EPOLL_CTL_DEL ? NULL : &events);
		//lprintf(__FILE__, __LINE__, error_level, "epoll_ctl(%d,%s)", pollable->get_handle(), op == EPOLL_CTL_ADD ? "add" : (op == EPOLL_CTL_MOD ? "mod" : (op == EPOLL_CTL_DEL ? "del" : "unknown")));
		if (r < 0) {
			lprintf(__FILE__, __LINE__, error_level, "epoll_ctl(%d) failed:%s", pollable->get_handle(), string_error(errno).c_str());
			return false;
		}
		if (op == EPOLL_CTL_ADD) {
			++count;
			lprintf(__FILE__, __LINE__, error_level, "epoll_ctl add %d", count);
			pollable->set_poll(self.lock());
		} else if (op == EPOLL_CTL_DEL) {
			if (0 < count) {
				--count;
			}
			lprintf(__FILE__, __LINE__, error_level, "epoll_ctl del %d", count);
			pollable->set_poll(std::shared_ptr<poll_type>());
		} else {
			//lputs(__FILE__, __LINE__, error_level, "epoll_ctl mod");
		}
		return true;
	}
	bool poll_type::wait(int timeout_milli_sec, int activate_count)
	{
		return false;
		if (count < activate_count) {
			activate_count = count;
		}
		if (activate_count <= 0) {
			return true;
		}
		if (events.size() < activate_count) {
			events.resize(activate_count + 16);
		}
		int r = epoll_wait(fd,  &events[0], activate_count, timeout_milli_sec);
		if (r < 0) {
			if (errno == EINTR) {
				//lputs(__FILE__, __LINE__, error_level, "EINTR");
				return true;
			}
			lprintf(__FILE__, __LINE__, error_level, "epoll_wait failed:%s", string_error(errno).c_str());
			return false;
		}
		if (activate_count < r) {
			lputs(__FILE__, __LINE__, error_level, "epoll_wait return incorrect size");
			r = activate_count;
		}
		for (auto it = events.begin(), end = events.begin() + r; it != end; ++it) {
			auto & event = *it;
			pollable_type * ptr = reinterpret_cast<pollable_type *>(event.data.ptr);
			if (ptr) {
				ptr->trigger(event.events);
			}
		}
		return true;
	}
	std::pair<pollable_type *, uint32_t> poll_type::wait_one(int timeout_milli_sec)
	{
		std::pair<pollable_type *, uint32_t> result;
		result.first = NULL;
		result.second = 0;
		if (count < 1) {
			return result;
		}
		epoll_event events[16];
		//mutex_locker locker(mutex);
		int r = epoll_wait(fd,  &events[0], 1, timeout_milli_sec);
		if (r < 0) {
			if (errno == EINTR) {
				lputs(__FILE__, __LINE__, error_level, "EINTR");
				return result;
			}
			lprintf(__FILE__, __LINE__, error_level, "epoll_wait failed:%s", string_error(errno).c_str());
			return result;
		}
		if (1 < r) {
			lputs(__FILE__, __LINE__, error_level, "epoll_wait return incorrect size");
			r = 1;
		}
		if (r == 1) {
			result.first = reinterpret_cast<pollable_type *>(events[0].data.ptr);
			result.second = events[0].events;
		}
		return result;
	}
}
