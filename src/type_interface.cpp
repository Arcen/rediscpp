#include "type_interface.h"

namespace rediscpp
{
	type_interface::type_interface(const timeval_type & current)
		: last_modified_time(current)
		, expire_time(0,0)
	{
	}
	type_interface::~type_interface()
	{
	}
	bool type_interface::is_expired(const timeval_type & current)
	{
		return ! expire_time.is_epoc() && expire_time <= current;
	}
	void type_interface::expire(const timeval_type & at)
	{
		expire_time = at;
	}
	void type_interface::persist()
	{
		expire_time.epoc();
	}
	bool type_interface::is_expiring() const
	{
		return ! expire_time.is_epoc();
	}
	timeval_type type_interface::get_last_modified_time() const
	{
		return last_modified_time;
	}
	timeval_type type_interface::ttl(const timeval_type & current) const
	{
		if (is_expiring()) {
			if (current < expire_time) {
				return expire_time - current;
			}
		}
		return timeval_type(0,0);
	}
	void type_interface::update(const timeval_type & current)
	{
		last_modified_time = current;
	}
};
