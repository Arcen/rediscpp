#ifndef INCLUDE_REDIS_CPP_EXPIRE_INFO_H
#define INCLUDE_REDIS_CPP_EXPIRE_INFO_H

#include "timeval.h"

namespace rediscpp
{
	class expire_info
	{
		timeval_type expire_time;///<0,0なら有効期限無し、消失する日時を保存する
	public:
		expire_info()
			: expire_time(0, 0)
		{
		}
		expire_info(const expire_info & rhs)
			: expire_time(rhs.expire_time)
		{
		}
		expire_info(const timeval_type & current)
			: expire_time(0, 0)
		{
		}
		~expire_info(){}
		expire_info & operator=(const expire_info & rhs)
		{
			if (this != &rhs) {
				expire_time = rhs.expire_time;
			}
			return *this;
		}
		void set(const expire_info & rhs)
		{
			if (this != &rhs) {
				expire_time = rhs.expire_time;
			}
		}
		bool is_expired(const timeval_type & current) const;
		void expire(const timeval_type & at);
		void persist();
		bool is_expiring() const;
		timeval_type ttl(const timeval_type & current) const;
		timeval_type at() const { expire_time; }
	};
};

#endif
