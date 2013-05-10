#ifndef INCLUDE_REDIS_CPP_FILE_H
#define INCLUDE_REDIS_CPP_FILE_H

#include "common.h"
#include "crc64.h"
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace rediscpp
{
	class file_type
	{
		int fd;
		crc64_type crc64;
		uint64_t crc;
		file_type();
		file_type(const file_type & rhs);
		file_type(int fd_)
			: fd(fd_)
			, crc(0)
		{
		}
	public:
		~file_type()
		{
			if (0 <= fd) {
				close(fd);
			}
		}
		static std::shared_ptr<file_type> create(const std::string & path)
		{
			int fd = ::creat(path.c_str(), 0666);
			if (fd < 0) {
				lprintf(__FILE__, __LINE__, error_level, "failed: creat(%s) : %s", path.c_str(), string_error(errno).c_str());
				return std::shared_ptr<file_type>();
			}
			return std::shared_ptr<file_type>(new file_type(fd));
		}
		void write(const std::string & str)
		{
			write(str.c_str(), str.size());
		}
		void write(const std::vector<uint8_t> & data)
		{
			if (!data.empty()) {
				write(&data[0], data.size());
			}
		}
		void printf(const char * fmt, ...)
		{
			va_list args;
			va_start(args, fmt);
			std::string str = vformat(fmt, args);
			va_end(args);
			write(str);
		}
		void write8(uint8_t value) { write(&value, 1); }
		void write16(uint16_t value) { write(&value, 2); }
		void write32(uint32_t value) { write(&value, 4); }
		void write64(uint64_t value) { write(&value, 8); }
		void write(const void * ptr, size_t len)
		{
			if (len) {
				crc = crc64.update(crc, ptr, len);
				ssize_t r = ::write(fd, ptr, len);
				if (r != len) {
					lprintf(__FILE__, __LINE__, error_level, "failed: write(%zd) : %s", len, string_error(errno).c_str());
					throw std::runtime_error("file io error");
				}
			}
		}
		void write_crc()
		{
			write64(htobe64(crc));
		}
	};
};

#endif
