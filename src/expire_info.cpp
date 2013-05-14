#include "expire_info.h"

namespace rediscpp
{
	bool expire_info::is_expired(const timeval_type & current) const
	{
		return ! expire_time.is_epoc() && expire_time <= current;
	}
	void expire_info::expire(const timeval_type & at)
	{
		expire_time = at;
	}
	void expire_info::persist()
	{
		expire_time.epoc();
	}
	bool expire_info::is_expiring() const
	{
		return ! expire_time.is_epoc();
	}
	timeval_type expire_info::ttl(const timeval_type & current) const
	{
		if (is_expiring()) {
			if (current < expire_time) {
				return expire_time - current;
			}
		}
		return timeval_type(0,0);
	}
};
