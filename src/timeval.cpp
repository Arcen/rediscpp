#include "timeval.h"
#include <sys/time.h>

namespace rediscpp
{
	timeval_type::timeval_type()
	{
		update();
	}
	void timeval_type::update()
	{
		int r = gettimeofday(this, NULL);
		if (r < 0) {
			throw std::runtime_error("ERR could not get time");
		}
	}
	timeval_type::timeval_type(const timeval_type & rhs)
	{
		tv_sec = rhs.tv_sec;
		tv_usec = rhs.tv_usec;
	}
	timeval_type::timeval_type(time_t sec, suseconds_t usec)
	{
		tv_sec = sec;
		tv_usec = usec;
	}
	timeval_type & timeval_type::operator=(const timeval_type & rhs)
	{
		tv_sec = rhs.tv_sec;
		tv_usec = rhs.tv_usec;
		return *this;
	}
	bool timeval_type::operator==(const timeval_type & rhs) const
	{
		return (tv_sec == rhs.tv_sec) && (tv_usec == rhs.tv_usec);
	}
	bool timeval_type::operator<(const timeval_type & rhs) const 
	{
		return (tv_sec != rhs.tv_sec) ? tv_sec < rhs.tv_sec : tv_usec < rhs.tv_usec;
	}
	timeval_type & timeval_type::operator+=(const timeval_type & rhs)
	{
		tv_sec += rhs.tv_sec;
		tv_usec += rhs.tv_usec;
		if (1000000 <= tv_usec) {
			tv_usec -= 1000000;
			++tv_sec;
		}
		return *this;
	}
	timeval_type & timeval_type::operator-=(const timeval_type & rhs)
	{
		tv_sec -= rhs.tv_sec;
		if (rhs.tv_usec <= tv_usec) {
			tv_usec -= rhs.tv_usec;
		} else {
			--tv_sec;
			tv_usec += 1000000 - rhs.tv_usec;
		}
		return *this;
	}
	void timeval_type::add_msec(int64_t msec)
	{
		*this += timeval_type(msec / 1000, (msec % 1000) * 1000);
	}
}
