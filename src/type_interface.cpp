#include "type_interface.h"

namespace rediscpp
{
	type_interface::type_interface()
	{
	}
	type_interface::type_interface(const timeval_type & current)
		: modified(current)
	{
	}
	type_interface::~type_interface()
	{
	}
	timeval_type type_interface::get_last_modified_time() const
	{
		return modified;
	}
	void type_interface::update(const timeval_type & current)
	{
		modified = current;
	}
};
