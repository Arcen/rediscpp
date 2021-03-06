#ifndef INCLUDE_REDIS_CPP_TIMEVAL_H
#define INCLUDE_REDIS_CPP_TIMEVAL_H

#include "common.h"

namespace rediscpp
{
	class timeval_type : public timeval
	{
	public:
		timeval_type();
		timeval_type(const timeval_type & rhs);
		timeval_type(time_t sec, suseconds_t usec);
		timeval_type & operator=(const timeval_type & rhs);
		void update();
		void add_msec(int64_t msec);
		bool operator==(const timeval_type & rhs) const;
		bool operator<(const timeval_type & rhs) const;
		timeval_type & operator+=(const timeval_type & rhs);
		timeval_type & operator-=(const timeval_type & rhs);
		bool operator!=(const timeval_type & rhs) const { return !(*this == rhs); }
		bool operator>=(const timeval_type & rhs) const { return !(*this < rhs); }
		bool operator>(const timeval_type & rhs) const { return (rhs < *this); }
		bool operator<=(const timeval_type & rhs) const { return !(rhs < *this); }
		timeval_type operator+(const timeval_type & rhs) const { return timeval_type(*this) += rhs; }
		timeval_type operator-(const timeval_type & rhs) const { return timeval_type(*this) -= rhs; }

		uint64_t get_ms() const
		{
			return static_cast<uint64_t>(tv_sec) * 1000ULL + static_cast<uint64_t>(tv_usec / 1000);
		}
		timespec get_timespec() const
		{
			timespec ts;
			ts.tv_sec = tv_sec;
			ts.tv_nsec = tv_usec * 1000;
			return ts;
		}
		bool is_epoc() const
		{
			return tv_sec == 0 && tv_usec == 0;
		}
		void epoc()
		{
			tv_sec = 0;
			tv_usec = 0;
		}
	};
}

#endif
