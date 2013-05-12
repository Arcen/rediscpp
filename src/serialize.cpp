#include "server.h"
#include "client.h"
#include "master.h"
#include "log.h"
#include "file.h"
#include "type_string.h"
#include "type_list.h"
#include "type_set.h"
#include "type_zset.h"
#include "type_hash.h"

namespace rediscpp
{
	enum constants {
		version = 6,
		op_eof = 255,
		op_selectdb = 254,
		op_expire = 253,
		op_expire_ms = 252,
		len_6bit = 0 << 6,
		len_14bit = 1 << 6,
		len_32bit = 2 << 6,
		double_nan = 253,
		double_pinf = 254,
		double_ninf = 255,
		int_type_string = 0,
		int_type_list = 1,
		int_type_set = 2,
		int_type_zset = 3,
		int_type_hash = 4,
	};

	static void write_len(std::shared_ptr<file_type> & f, uint32_t len)
	{
		if (len < 0x40) {//6bit (8-2)
			f->write8(len/* | len_6bit*/);
		} else if (len < 0x4000) {//14bit (16-2)
			f->write8((len >> 8) | len_14bit);
			f->write8(len & 0xFF);
		} else {
			f->write8(len_32bit);
			f->write(&len, 4);
		}
	}
	static void write_string(std::shared_ptr<file_type> & dst, const std::string & str)
	{
		write_len(dst, str.size());
		dst->write(str);
	}
	static void write_double(std::shared_ptr<file_type> & dst, double val)
	{
		if (isnan(val)) {
			dst->write8(double_nan);
		} else if (isinf(val)) {
			dst->write8(val < 0 ? double_ninf : double_pinf);
		} else {
			const std::string & str = std::move(format("%.17g", val));
			dst->write8(str.size());
			write_string(dst, str);
		}
	}
	static void write_len(std::string & dst, uint32_t len)
	{
		if (len < 0x40) {//6bit (8-2)
			dst.push_back(len/* | len_6bit*/);
		} else if (len < 0x4000) {//14bit (16-2)
			dst.push_back((len >> 8) | len_14bit);
			dst.push_back(len & 0xFF);
		} else {
			dst.push_back(len_32bit);
			dst.insert(dst.end(), reinterpret_cast<char*>(&len), reinterpret_cast<char*>(&len) + 4);
		}
	}
	static void write_string(std::string & dst, const std::string & str)
	{
		write_len(dst, str.size());
		dst.insert(dst.end(), str.begin(), str.end());
	}
	static void write_double(std::string & dst, double val)
	{
		if (isnan(val)) {
			dst.push_back(double_nan);
		} else if (isinf(val)) {
			dst.push_back(val < 0 ? double_ninf : double_pinf);
		} else {
			const std::string & str = std::move(format("%.17g", val));
			dst.push_back(str.size());
			write_string(dst, str);
		}
	}
	static uint32_t read_len(std::shared_ptr<file_type> & src)
	{
		uint8_t head = src->read8();
		switch (head & 0xC0) {
		case len_6bit:
			return head & 0x3F;
		case len_14bit:
			return ((head & 0x3F) << 8) | src->read8();
		case len_32bit:
			return src->read32();
		default:
			throw std::runtime_error("length invalid");
		}
	}
	static std::string read_string(std::shared_ptr<file_type> & src)
	{
		uint32_t len = read_len(src);
		if (!len) return std::move(std::string());
		std::string str(len, '\0');
		src->read(&str[0], len);
		return std::move(str);
	}
	static double read_double(std::shared_ptr<file_type> & src)
	{
		uint8_t head = src->read8();
		switch (head) {
		case double_nan:
			return strtod("nan", NULL);
		case double_ninf:
			return strtod("-inf", NULL);
		case double_pinf:
			return strtod("inf", NULL);
		case 0:
			throw std::runtime_error("invalid double");
		}
		std::string str(head, '\0');
		src->read(&str[0], head);
		bool is_valid = true;
		double d = atod(str, is_valid);
		if (!is_valid) {
			throw std::runtime_error("invalid double");
		}
		return d;
	}
	static uint32_t read_len(std::string::const_iterator & it, std::string::const_iterator end)
	{
		if (it == end) {
			throw std::runtime_error("not enough");
		}
		uint8_t head = *it++;
		switch (head & 0xC0) {
		case len_6bit:
			return head & 0x3F;
		case len_14bit:
			if (it == end) {
				throw std::runtime_error("not enough");
			}
			return ((head & 0x3F) << 8) | (*it++);
		case len_32bit:
			{
				uint32_t value = 0;
				for (int i = 0; i < 4; ++i) {
					if (it == end) {
						throw std::runtime_error("not enough");
					}
					value |= (*it++) << (8 * i);
				}
				return value;
			}
		default:
			throw std::runtime_error("length invalid");
		}
	}
	static std::string read_string(std::string::const_iterator & it, std::string::const_iterator end)
	{
		uint32_t len = read_len(it, end);
		if (!len) return std::move(std::string());
		if (std::distance(it, end) < len) {
			throw std::runtime_error("not enough");
		}
		std::string str(it, it + len);
		it += len;
		return std::move(str);
	}
	static double read_double(std::string::const_iterator & it, std::string::const_iterator end)
	{
		if (it == end) {
			throw std::runtime_error("not enough");
		}
		uint8_t head = *it++;
		switch (head) {
		case double_nan:
			return strtod("nan", NULL);
		case double_ninf:
			return strtod("-inf", NULL);
		case double_pinf:
			return strtod("inf", NULL);
		case 0:
			throw std::runtime_error("invalid double");
		}
		if (std::distance(it, end) < head) {
			throw std::runtime_error("not enough");
		}
		std::string str(it, it + head);
		it += head;
		bool is_valid = true;
		double d = atod(str, is_valid);
		if (!is_valid) {
			throw std::runtime_error("invalid double");
		}
		return d;
	}
	void server_type::dump(std::string & dst, const std::shared_ptr<type_interface> & value)
	{
		dst.reserve(1024);
		switch (value->get_int_type()) {
		case int_type_string:
			{
				std::shared_ptr<type_string> str = std::dynamic_pointer_cast<type_string>(value);
				write_string(dst, str->get());
			}
			break;
		case int_type_list:
			{
				std::shared_ptr<type_list> list = std::dynamic_pointer_cast<type_list>(value);
				write_len(dst, list->size());
				auto range = list->get_range();
				for (auto it = range.first; it != range.second; ++it) {
					auto & str = *it;
					write_string(dst, str);
				}
			}
			break;
		case int_type_set:
			{
				std::shared_ptr<type_set> set = std::dynamic_pointer_cast<type_set>(value);
				write_len(dst, set->size());
				auto range = set->smembers();
				for (auto it = range.first; it != range.second; ++it) {
					auto & str = *it;
					write_string(dst, str);
				}
			}
			break;
		case int_type_zset:
			{
				std::shared_ptr<type_zset> zset = std::dynamic_pointer_cast<type_zset>(value);
				write_len(dst, zset->size());
				auto range = zset->zrange();
				for (auto it = range.first; it != range.second; ++it) {
					auto & pair = *it;
					write_string(dst, pair->member);
					write_double(dst, pair->score);
				}
			}
			break;
		case int_type_hash:
			{
				std::shared_ptr<type_hash> hash = std::dynamic_pointer_cast<type_hash>(value);
				write_len(dst, hash->size());
				auto range = hash->hgetall();
				for (auto it = range.first; it != range.second; ++it) {
					auto & pair = *it;
					write_string(dst, pair.first);
					write_string(dst, pair.second);
				}
			}
			break;
		}
	}
	std::shared_ptr<type_interface> server_type::restore(const std::string & src, const timeval_type & current)
	{
		std::shared_ptr<type_interface> value;
		if (src.empty()) {
			return value;
		}
		std::string::const_iterator it = src.begin(), end = src.end();
		switch (*it++) {
		case int_type_string:
			{
				auto strval = read_string(it, end);
				std::shared_ptr<type_string> str(new type_string(strval, current));
				value = str;
			}
			break;
		case int_type_list:
			{
				std::shared_ptr<type_list> list(new type_list(current));
				uint32_t count = read_len(it, end);
				for (uint32_t i = 0; i < count; ++i) {
					auto strval = read_string(it, end);
					list->rpush(strval);
				}
				value = list;
			}
			break;
		case int_type_set:
			{
				std::shared_ptr<type_set> set(new type_set(current));
				uint32_t count = read_len(it, end);
				std::vector<std::string *> members(1, NULL);
				for (uint32_t i = 0; i < count; ++i) {
					auto strval = read_string(it, end);
					members[0] = &strval;
					set->sadd(members);
				}
				value = set;
			}
			break;
		case int_type_zset:
			{
				std::shared_ptr<type_zset> zset(new type_zset(current));
				uint32_t count = read_len(it, end);
				std::vector<double> scores(1, NULL);
				std::vector<std::string *> members(1, NULL);
				for (uint32_t i = 0; i < count; ++i) {
					auto strval = read_string(it, end);
					scores[0] = read_double(it, end);
					members[0] = &strval;
					zset->zadd(scores, members);
				}
				value = zset;
			}
			break;
		case int_type_hash:
			{
				std::shared_ptr<type_hash> hash(new type_hash(current));
				uint32_t count = read_len(it, end);
				for (uint32_t i = 0; i < count; ++i) {
					auto strkey = read_string(it, end);
					auto strval = read_string(it, end);
					hash->hset(strkey, strval);
				}
				value = hash;
			}
			break;
		}
		if (std::distance(it, end) != 2 + 8) {
			throw std::runtime_error("suffix error");
		}
		uint16_t ver = *it++;
		ver |= (*it++) << 8;
		if (version < ver) {
			throw std::runtime_error("version error");
		}
		uint64_t src_crc = 0;
		for (int i = 0; i < 8; ++i) {
			uint64_t val = static_cast<uint64_t>(*it++) & 0xFF;
			val <<= i * 8;
			src_crc |= val;
		}
		uint64_t crc = crc64::update(0, &src[0], src.size() - 8);
		if (src_crc != crc) {
			throw std::runtime_error(format("crc error %"PRIx64" != %"PRIx64, src_crc, crc));
		}
		return value;
	}
	bool server_type::save(const std::string & path)
	{
		std::shared_ptr<file_type> f = file_type::create(path, true);
		if (!f) {
			return false;
		}
		try {
			timeval_type current;
			f->printf("REDIS%04d", version);
			for (size_t i = 0, n = databases.size(); i < n; ++i ) {
				auto & db = *databases[i];
				auto range = db.range();
				if (range.first == range.second) {
					continue;
				}
				//selectdb i
				f->write8(op_selectdb);
				write_len(f, i);

				for (auto it = range.first; it != range.second; ++it) {
					auto & kv = *it;
					auto & key = kv.first;
					auto & value = kv.second;
					if (value->is_expired(current)) {
						continue;
					}
					if (value->is_expiring()) {
						f->write8(op_expire_ms);
						f->write64(value->at().get_ms());
					}
					const int value_type = value->get_int_type();
					f->write8(value_type);
					write_string(f, key);
					std::string value_str;
					dump(value_str, value);
					f->write(value_str);
				}
			}
			f->write8(op_eof);
			f->write_crc();
		} catch (std::exception e) {
			::unlink(path.c_str());
			return false;
		}
		return true;
	}
	bool server_type::load(const std::string & path)
	{
		std::shared_ptr<file_type> f = file_type::open(path, true);
		if (!f) {
			return false;
		}
		try {
			timeval_type current;
			char buf[128] = {0};
			f->read(buf, 9);
			if (memcmp(buf, "REDIS", 5)) {
				lprintf(__FILE__, __LINE__, info_level, "Not found REDIS header");
				throw std::runtime_error("Not found REDIS header");
			}
			int ver = atoi(buf + 5);
			if (ver < 0 || version < ver) {
				lprintf(__FILE__, __LINE__, info_level, "Not found REDIS version %d", ver);
				throw std::runtime_error("Not compatible REDIS version");
			}
			//全ロック
			std::vector<std::shared_ptr<database_write_locker>> lockers(databases.size());
			for (size_t i = 0; i < databases.size(); ++i) {
				lockers[i].reset(new database_write_locker(databases[i].get(), NULL, false));
			}
			//@todo ファイルから読むのがslaveof以外でおきるなら、ここは修正が必要
			slave = true;
			for (int i = 0, n = databases.size(); i < n; ++i) {
				auto & db = *(lockers[i]);
				db->clear();
			}
			uint8_t op = 0;
			uint32_t db_index = 0;
			auto db = databases[db_index];
			uint64_t expire_at = 0;
			while (op != op_eof) {
				op = f->read8();
				switch (op) {
				case op_eof:
					if (!f->check_crc()) {
						lprintf(__FILE__, __LINE__, info_level, "corrupted crc");
						throw std::runtime_error("corrupted crc");
					}
					continue;
				case op_selectdb:
					db_index = read_len(f);
					if (databases.size() <= db_index) {
						lprintf(__FILE__, __LINE__, info_level, "db index out of range");
						throw std::runtime_error("db index out of range");
					}
					db = databases[db_index];
					continue;
				case op_expire_ms:
					expire_at = f->read64();
					continue;
				default:
					{
						std::string key = read_string(f);
						std::shared_ptr<type_interface> value;
						switch (op) {
						case int_type_string:
							{
								std::shared_ptr<type_string> str(new type_string(read_string(f), current));
								value = str;
							}
							break;
						case int_type_list:
							{
								std::shared_ptr<type_list> list(new type_list(current));
								value = list;
								uint32_t count = read_len(f);
								for (uint32_t i = 0; i < count; ++i) {
									list->rpush(read_string(f));
								}
							}
							break;
						case int_type_set:
							{
								std::shared_ptr<type_set> set(new type_set(current));
								value = set;
								uint32_t count = read_len(f);
								std::vector<std::string *> members(1, NULL);
								for (uint32_t i = 0; i < count; ++i) {
									auto strval = std::move(read_string(f));
									members[0] = &strval;
									set->sadd(members);
								}
							}
							break;
						case int_type_zset:
							{
								std::shared_ptr<type_zset> zset(new type_zset(current));
								value = zset;
								uint32_t count = read_len(f);
								std::vector<double> scores(1, NULL);
								std::vector<std::string *> members(1, NULL);
								for (uint32_t i = 0; i < count; ++i) {
									auto strval = std::move(read_string(f));
									scores[0] = read_double(f);
									members[0] = &strval;
									zset->zadd(scores, members);
								}
							}
							break;
						case int_type_hash:
							{
								std::shared_ptr<type_hash> hash(new type_hash(current));
								value = hash;
								uint32_t count = read_len(f);
								for (uint32_t i = 0; i < count; ++i) {
									auto strkey = std::move(read_string(f));
									auto strval = std::move(read_string(f));
									hash->hset(strkey, strval);
								}
							}
							break;
						}
						if (expire_at) {
							value->expire(timeval_type(expire_at / 1000, (expire_at % 1000) * 1000));
							expire_at = 0;
						}
						db->insert(key, value, current);
					}
					break;
				}
			}
		} catch (const std::exception & e) {
			lprintf(__FILE__, __LINE__, info_level, "exception:%s", e.what());
			return false;
		} catch (...) {
			lprintf(__FILE__, __LINE__, info_level, "exception");
			return false;
		}
		return true;
	}
	void server_type::dump_suffix(std::string & dst)
	{
		dst.push_back(version & 0xFF);
		dst.push_back((version >> 8) & 0xFF);
		uint64_t crc = crc64::update(0, &dst[0], dst.size());
		dst.insert(dst.end(), reinterpret_cast<char*>(&crc), reinterpret_cast<char*>(&crc) + 8);
	}
}
