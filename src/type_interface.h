#ifndef INCLUDE_REDIS_CPP_TYPE_INTERFACE_H
#define INCLUDE_REDIS_CPP_TYPE_INTERFACE_H

#include "common.h"
#include "timeval.h"

namespace rediscpp
{
	class type_string;
	class type_list;
	class type_hash;
	class type_set;
	class type_zset;
	class type_interface
	{
	protected:
		timeval_type last_modified_time;///<�Ō�ɏC����������(WATCH�p)
		timeval_type expire_time;///<0,0�Ȃ�L�����������A�������������ۑ�����
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
