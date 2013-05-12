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
		bool checking_crc;
		std::string path;
		bool unlink_on_close;
		file_type();
		file_type(const file_type & rhs);
		file_type(int fd_, bool checking_crc_ = false)
			: fd(fd_)
			, crc(0)
			, checking_crc(checking_crc_)
			, unlink_on_close(false)
		{
		}
	public:
		~file_type()
		{
			if (0 <= fd) {
				close(fd);
				if (unlink_on_close) {
					int r = ::unlink(path.c_str());
					if (r < 0) {
						lprintf(__FILE__, __LINE__, error_level, "failed: unlink(%s) : %s", path.c_str(), string_error(errno).c_str());
					}
				}
			}
		}
		static std::shared_ptr<file_type> create(const std::string & path, bool checking_crc_ = false)
		{
			int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC | O_LARGEFILE, 0666);
			if (fd < 0) {
				lprintf(__FILE__, __LINE__, error_level, "failed: create(%s) : %s", path.c_str(), string_error(errno).c_str());
				return std::shared_ptr<file_type>();
			}
			return std::shared_ptr<file_type>(new file_type(fd, checking_crc_));
		}
		static std::shared_ptr<file_type> open(const std::string & path, bool checking_crc_ = false, bool unlink_on_close_ = false)
		{
			int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_LARGEFILE);
			if (fd < 0) {
				lprintf(__FILE__, __LINE__, error_level, "failed: open(%s) : %s", path.c_str(), string_error(errno).c_str());
				return std::shared_ptr<file_type>();
			}
			std::shared_ptr<file_type> result(new file_type(fd, checking_crc_));
			if (unlink_on_close_) {
				result->unlink_on_close = true;
				result->path = path;
			}
			return result;
		}
		int get_fd() const { return fd; }
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
				if (checking_crc) {
					crc = crc64.update(crc, ptr, len);
				}
				ssize_t r = ::write(fd, ptr, len);
				if (r != len) {
					lprintf(__FILE__, __LINE__, error_level, "failed: write(%"PRId64") : %s", len, string_error(errno).c_str());
					throw std::runtime_error("file io error");
				}
			}
		}
		void write_crc()
		{
			write64(htobe64(crc));
		}
		bool check_crc()
		{
			uint64_t check = htobe64(crc);
			return read64() == check;
		}
		void read(void * ptr, size_t len)
		{
			if (len) {
				ssize_t r = ::read(fd, ptr, len);
				if (r != len) {
					lprintf(__FILE__, __LINE__, error_level, "failed: read(%"PRId64") : %s", len, string_error(errno).c_str());
					throw std::runtime_error("file io error");
				}
				if (checking_crc) {
					crc = crc64.update(crc, ptr, len);
				}
			}
		}
		uint8_t read8()
		{
			uint8_t value;
			read(&value, 1);
			return value;
		}
		uint16_t read16()
		{
			uint16_t value;
			read(&value, 2);
			return value;
		}
		uint32_t read32()
		{
			uint32_t value;
			read(&value, 4);
			return value;
		}
		uint64_t read64()
		{
			uint64_t value;
			read(&value, 8);
			return value;
		}
		void flush()
		{
			int r = ::fsync(fd);
			if (r < 0) {
				lprintf(__FILE__, __LINE__, error_level, "failed: fsync : %s", string_error(errno).c_str());
			}
		}
		size_t size()
		{
			struct stat st;
			int r = ::fstat(fd, &st);
			if (r < 0) {
				lprintf(__FILE__, __LINE__, error_level, "failed: fstat : %s", string_error(errno).c_str());
				return 0;
			}
			return st.st_size;
		}
	};
};

#endif
