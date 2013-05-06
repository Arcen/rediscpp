#ifndef INCLUDE_REDIS_CPP_TYPE_INTERFACE_H
#define INCLUDE_REDIS_CPP_TYPE_INTERFACE_H

#include "common.h"
#include "timeval.h"

namespace rediscpp
{
	class string_type;
	class list_type;
	class hash_type;
	class set_type;
	class zset_type;
	class type_interface
	{
	protected:
		timeval_type last_modified_time;///<ÅŒã‚ÉC³‚µ‚½ŽžŠÔ(WATCH—p)
		timeval_type expire_time;///<0,0‚È‚ç—LŒøŠúŒÀ–³‚µAÁŽ¸‚·‚é“úŽž‚ð•Û‘¶‚·‚é
	public:
		type_interface(const timeval_type & current);
		virtual ~type_interface();
		virtual std::string get_type() = 0;
		bool is_expired(const timeval_type & current);
		void expire(const timeval_type & at);
		void persist();
		bool is_expiring() const;
		timeval_type get_last_modified_time() const;
		timeval_type ttl(const timeval_type & current) const;
		void update(const timeval_type & current);
	};
};

#endif
