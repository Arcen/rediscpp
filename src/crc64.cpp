#include "crc64.h"

namespace rediscpp
{
	//crc 64 jones http://www0.cs.ucl.ac.uk/staff/d.jones/crcnote.pdf
	//redis test e9c6d914c4b8d9ca = crc64("123456789")
	uint64_t crc64_type::table[256];
	bool crc64_type::initialized = false;
	crc64_type::crc64_type()
	{
		if (!initialized) {
			for (int i = 0; i < 256; ++i) {
				uint64_t c = i;
				for (int j = 0; j < 8; ++j) {
					if (c & 1) {
						c = 0x95AC9329AC4BC9B5ULL ^ (c >> 1);
					} else {
						c >>= 1;
					}
				}
				table[i] = c;
			}
			initialized = true;
		}
	}
	uint64_t crc64_type::update(uint64_t crc, const void * buf_, size_t len)
	{
		const uint8_t * buf = reinterpret_cast<const uint8_t*>(buf_);
		uint64_t c = crc;
		for (int i = 0; i < len; ++i) {
			c = table[(c ^ buf[i]) & 0xff] ^ (c >> 8);
		}
		return c;
	}
};
