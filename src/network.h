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
#include <sys/eventfd.h>
#include <sys/timerfd.h>

#include "common.h"
#include "thread.h"
#include "log.h"

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
		uint16_t get_port() const;
		sa_family_t get_family() const;
		void set_family(sa_family_t family);
		sockaddr * get_sockaddr();
		const sockaddr * get_sockaddr() const;
		size_t get_sockaddr_size() const;
		std::string get_info() const;
	};

	class poll_type;
	class pollable_type
	{
		friend class poll_type;
		pollable_type();
		pollable_type(const pollable_type &);
	public:
		typedef std::function<void(pollable_type * p,int)> callback_function_type;
	private:
		callback_function_type callback_function;
		std::weak_ptr<poll_type> poll;
	protected:
		int fd;
		epoll_event events;
		void * extra;
		void * extra2;
		std::weak_ptr<pollable_type> self;
	public:
		pollable_type(int fd_)
			: fd(fd_)
			, extra(0)
			, extra2(0)
		{
			memset(&events, 0, sizeof(events));
			events.data.ptr = this;
		}
		virtual ~pollable_type()
		{
			if (0 <= fd) {
				lprintf(__FILE__, __LINE__, error_level, "fd is not closed");
				::close(fd);
				fd = -1;
			}
		}
		void close();
		void trigger(int flag)
		{
			if (callback_function) {
				callback_function(this, flag);
			}
		}
		void set_callback(callback_function_type function)
		{
			callback_function = function;
		}
		void set_poll(std::shared_ptr<poll_type> poll_)
		{
			poll = poll_;
		}
		void set_extra(void * extra_) { extra = extra_; }
		void * get_extra() { return extra; }
		void set_extra2(void * extra2_) { extra2 = extra2_; }
		void * get_extra2() { return extra2; }
		int get_handle() const { return fd; }
		virtual uint32_t get_events() = 0;
		void mod();
	};
	class socket_type : public pollable_type
	{
		std::shared_ptr<address_type> local;
		std::shared_ptr<address_type> peer;///<リモートアドレス
		bool finished_to_read;
		bool finished_to_write;
		int shutdowning;
		bool broken;
		friend class poll_type;
		socket_type();
		socket_type(int s);
	public:
		~socket_type();
		bool shutdown(bool reading, bool writing);
		static std::shared_ptr<socket_type> create(const address_type & address, bool stream = true);
		bool set_nonblocking(bool nonblocking = true);
		bool set_reuse(bool reuse = true);
		bool set_nodelay(bool nodelay = true);
		bool set_keepalive(bool keepalive, int idle, int interval, int count);
		bool bind(std::shared_ptr<address_type> address);
		bool connect(std::shared_ptr<address_type> address);
		bool is_connected();
		bool listen(int queue_count);
		std::shared_ptr<socket_type> accept();
		bool is_broken() const { return broken; }
	private:
		std::deque<uint8_t> recv_buffer;
		std::deque<std::pair<std::vector<uint8_t>,size_t>> send_buffers;
		int sending_file_id;
		size_t sending_file_size;
		size_t sent_file_size;
	public:
		bool should_send() const { return (! send_buffers.empty() || is_sendfile()) && ! is_write_shutdowned(); }
		bool should_recv() const { return ! recv_buffer.empty() && ! is_read_shutdowned(); }
		bool send();
		bool recv();
		bool send(const void * buf, size_t len);
		void sendfile(int in_fd, size_t size)
		{
			sending_file_id = in_fd;
			sending_file_size = size;
			sent_file_size = 0;
		}
		bool is_sendfile() const { return sent_file_size < sending_file_size; }
		std::deque<uint8_t> & get_recv() { return recv_buffer; }
		bool recv_done() const { return finished_to_read; }
		std::shared_ptr<socket_type> get() { return std::dynamic_pointer_cast<socket_type>(self.lock()); }
		void close_after_send();
		bool is_read_shutdowned() const { return shutdowning == SHUT_RD || shutdowning == SHUT_RDWR; }
		bool is_write_shutdowned() const { return shutdowning == SHUT_WR || shutdowning == SHUT_RDWR; }
		virtual uint32_t get_events()
		{
			if (local.get()) {//server
				return EPOLLIN;
			}
			return EPOLLIN | EPOLLET | EPOLLONESHOT | (should_send() ? EPOLLOUT : 0);
		}
		bool done() const { return recv_done() && ! should_recv() && ! should_send(); }
		std::string get_peer_info() { return peer ? peer->get_info() : std::string(); }
	};
	class event_type : public pollable_type
	{
		mutex_type mutex;
		event_type(const event_type &);
		event_type();
		event_type(int fd_)
			: pollable_type(fd_)
		{
		}
	public:
		~event_type()
		{
			close();
		}
		static std::shared_ptr<event_type> create()
		{
			int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
			if (fd < 0) {
				switch (errno) {
				//case EINVAL:
				//case EMFILE:
				//case ENFILE:
				//case ENODEV:
				//case ENOMEM:
				default:
					throw std::runtime_error("::eventfd failed:" + string_error(errno));
				}
			}
			std::shared_ptr<event_type> result(new event_type(fd));
			result->self = result;
			return result;
		}
		bool send()
		{
			mutex_locker locker(mutex);
			uint64_t increment = 1;
			int r = write(fd, &increment, sizeof(increment));
			if (r == sizeof(increment)) {
				return true;
			}
			if (r < 0) {
				switch (errno) {
				case EAGAIN://加算結果がオーバーフローする
					return true;
				default:
					lprintf(__FILE__, __LINE__, error_level, "::write failed:%s", string_error(errno).c_str());
					return false;
				}
			}
			lprintf(__FILE__, __LINE__, error_level, "::write failed: r(%d) < sizeof(uint64_t)", r);
			return false;
		}
		bool recv()
		{
			mutex_locker locker(mutex);
			int interupt_count = 0;
			while (true) {
				uint64_t counter = 0;
				int r = read(fd, &counter, sizeof(counter));
				if (r == sizeof(counter)) {
					return 0 < counter;
				}
				if (r < 0) {
					switch (errno) {
					case EINTR:
						if (interupt_count < 3) {
							++interupt_count;
							continue;
						}
						return false;
					case EAGAIN://イベントが発生していない
						return false;
					default:
						lprintf(__FILE__, __LINE__, error_level, "::read failed:%s", string_error(errno).c_str());
						return false;
					}
				}
				lprintf(__FILE__, __LINE__, error_level, "::read failed: r(%d) < sizeof(uint64_t)", r);
				break;
			}
			return false;
		}
		virtual uint32_t get_events()
		{
			return EPOLLIN | EPOLLET | EPOLLONESHOT;
		}
	};
	class timer_type : public pollable_type
	{
		mutex_type mutex;
		timer_type(const timer_type &);
		timer_type();
		timer_type(int fd_)
			: pollable_type(fd_)
			, mutex(true)
		{
		}
	public:
		~timer_type()
		{
			close();
		}
		static std::shared_ptr<timer_type> create(bool monotonic = true)
		{
			int fd = timerfd_create(monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
			if (fd < 0) {
				switch (errno) {
				default:
					throw std::runtime_error("::timerfd_create failed:" + string_error(errno));
				}
			}
			std::shared_ptr<timer_type> result(new timer_type(fd));
			result->self = result;
			return result;
		}
		bool get(time_t & initial_sec, long & initial_nsec, time_t & periodic_sec, long & periodic_nsec)
		{
			itimerspec its;
			int r = 0;
			{
				mutex_locker locker(mutex);
				r = timerfd_gettime(fd, &its);
			}
			if (r < 0) {
				switch (errno) {
				default:
					lprintf(__FILE__, __LINE__, error_level, "::timerfd_gettime failed:%s", string_error(errno).c_str());
					return false;
				}
			}
			initial_sec = its.it_value.tv_sec;
			initial_nsec = its.it_value.tv_nsec;
			periodic_sec = its.it_interval.tv_sec;
			periodic_nsec = its.it_interval.tv_nsec;
			return true;
		}
		bool set(time_t initial_sec, long initial_nsec, time_t periodic_sec, long periodic_nsec)
		{
			itimerspec its;
			its.it_value.tv_sec = initial_sec;
			its.it_value.tv_nsec = initial_nsec;
			its.it_interval.tv_sec = periodic_sec;
			its.it_interval.tv_nsec = periodic_nsec;
			int r = 0;
			{
				mutex_locker locker(mutex);
				r = timerfd_settime(fd, 0, &its, NULL);
			}
			if (r < 0) {
				switch (errno) {
				default:
					lprintf(__FILE__, __LINE__, error_level, "::timerfd_settime failed:%s", string_error(errno).c_str());
					return false;
				}
			}
			return true;
		}
		bool stop()
		{
			return set(0, 0, 0, 0);
		}
		bool start(time_t sec, long nsec, bool once)
		{
			return set(sec, nsec, once ? 0 : sec, once ? 0 : nsec);
		}
		bool insert(time_t sec, long nsec)
		{
			time_t initial_sec, periodic_sec;
			long initial_nsec, periodic_nsec;
			mutex_locker locker(mutex);
			//lprintf(__FILE__, __LINE__, debug_level, "timer insert %d[sec]", (int)sec);
			if (!get(initial_sec, initial_nsec, periodic_sec, periodic_nsec)) {
				return false;
			}
			const bool started = (initial_sec != 0 && initial_nsec != 0);
			if (!started) {
				//lprintf(__FILE__, __LINE__, debug_level, "timer not started, start %d[sec]", (int)sec);
				return start(sec, nsec, true);
			}
			if (sec < initial_sec || (sec == initial_sec && nsec < nsec)) {//more fast first interval
				//lprintf(__FILE__, __LINE__, debug_level, "timer started but too late %d[sec], start %d[sec]", (int)initial_sec, (int)sec);
				return set(sec, nsec, periodic_sec, periodic_nsec);
			}
			//lprintf(__FILE__, __LINE__, debug_level, "timer started and near %d[sec], start %d[sec]", (int)initial_sec, (int)sec);
			return true;
		}
		bool is_start()
		{
			time_t initial_sec, periodic_sec;
			long initial_nsec, periodic_nsec;
			if (!get(initial_sec, initial_nsec, periodic_sec, periodic_nsec)) {
				return false;
			}
			return initial_sec != 0 && initial_nsec != 0;
		}
		bool is_periodic()
		{
			time_t initial_sec, periodic_sec;
			long initial_nsec, periodic_nsec;
			if (!get(initial_sec, initial_nsec, periodic_sec, periodic_nsec)) {
				return false;
			}
			return initial_nsec != 0 && periodic_nsec != 0;
		}
		bool recv()
		{
			mutex_locker locker(mutex);
			int interupt_count = 0;
			while (true) {
				uint64_t counter = 0;
				int r = read(fd, &counter, sizeof(counter));
				if (r == sizeof(counter)) {
					return true;
				}
				if (r < 0) {
					switch (errno) {
					case EINTR:
						if (interupt_count < 3) {
							++interupt_count;
							continue;
						}
						return false;
					case EAGAIN://イベントが発生していない
						return false;
					default:
						lprintf(__FILE__, __LINE__, error_level, "::read failed:%s", string_error(errno).c_str());
						return false;
					}
				}
				lprintf(__FILE__, __LINE__, error_level, "::read failed: r(%d) < sizeof(uint64_t)", r);
				break;
			}
			return false;
		}
		virtual uint32_t get_events()
		{
			return EPOLLIN | EPOLLET | EPOLLONESHOT;
		}
	};

	class poll_type
	{
		int fd;
		int count;
		std::weak_ptr<poll_type> self;
		mutex_type mutex;
		poll_type();
	public:
		static std::shared_ptr<poll_type> create();
		~poll_type();
		void close();
	private:
		bool operation(std::shared_ptr<pollable_type> pollable, int op);
	public:
		int get_count() const { return count; }
		bool append(std::shared_ptr<pollable_type> pollable) { return operation(pollable, EPOLL_CTL_ADD); }
		bool modify(std::shared_ptr<pollable_type> pollable) { return operation(pollable, EPOLL_CTL_MOD); }
		bool remove(std::shared_ptr<pollable_type> pollable) { return operation(pollable, EPOLL_CTL_DEL); }
		bool wait(std::vector<epoll_event> & events, int timeout_milli_sec = 0);
	};
}

#endif
