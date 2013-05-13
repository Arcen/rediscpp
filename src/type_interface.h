#ifndef INCLUDE_REDIS_CPP_TYPE_INTERFACE_H
#define INCLUDE_REDIS_CPP_TYPE_INTERFACE_H

#include "common.h"

namespace rediscpp
{
	class type_string;
	class type_list;
	class type_hash;
	class type_set;
	class type_zset;
	class file_type;
	enum type_types {
		string_type = 0,
		list_type = 1,
		set_type = 2,
		zset_type = 3,
		hash_type= 4,
	};
	class type_interface
	{
	public:
		type_interface();
		virtual ~type_interface();
		virtual type_types get_type() const = 0;
		virtual void output(std::shared_ptr<file_type> & dst) const = 0;
		virtual void output(std::string & dst) const = 0;
		static void write_len(std::shared_ptr<file_type> & dst, uint32_t len);
		static void write_string(std::shared_ptr<file_type> & dst, const std::string & str);
		static void write_double(std::shared_ptr<file_type> & dst, double val);
		static void write_len(std::string & dst, uint32_t len);
		static void write_string(std::string & dst, const std::string & str);
		static void write_double(std::string & dst, double val);
		static uint32_t read_len(std::shared_ptr<file_type> & src);
		static std::string read_string(std::shared_ptr<file_type> & src);
		static double read_double(std::shared_ptr<file_type> & src);
		static uint32_t read_len(std::pair<std::string::const_iterator,std::string::const_iterator> & src);
		static std::string read_string(std::pair<std::string::const_iterator,std::string::const_iterator> & src);
		static double read_double(std::pair<std::string::const_iterator,std::string::const_iterator> & src);
	};
};

#endif
