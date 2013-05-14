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
	};

	void type_interface::write_len(std::shared_ptr<file_type> & dst, uint32_t len)
	{
		if (len < 0x40) {//6bit (8-2)
			dst->write8(len/* | len_6bit*/);
		} else if (len < 0x4000) {//14bit (16-2)
			dst->write8((len >> 8) | len_14bit);
			dst->write8(len & 0xFF);
		} else {
			dst->write8(len_32bit);
			dst->write(&len, 4);
		}
	}
	void type_interface::write_string(std::shared_ptr<file_type> & dst, const std::string & str)
	{
		write_len(dst, str.size());
		dst->write(str);
	}
	void type_interface::write_double(std::shared_ptr<file_type> & dst, double val)
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
	void type_interface::write_len(std::string & dst, uint32_t len)
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
	void type_interface::write_string(std::string & dst, const std::string & str)
	{
		write_len(dst, str.size());
		dst.insert(dst.end(), str.begin(), str.end());
	}
	void type_interface::write_double(std::string & dst, double val)
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
	uint32_t type_interface::read_len(std::shared_ptr<file_type> & src)
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
	std::string type_interface::read_string(std::shared_ptr<file_type> & src)
	{
		uint32_t len = read_len(src);
		if (!len) return std::move(std::string());
		std::string str(len, '\0');
		src->read(&str[0], len);
		return std::move(str);
	}
	double type_interface::read_double(std::shared_ptr<file_type> & src)
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
	uint32_t type_interface::read_len(std::pair<std::string::const_iterator,std::string::const_iterator> & src)
	{
		auto & it = src.first;
		auto & end = src.second;
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
	std::string type_interface::read_string(std::pair<std::string::const_iterator,std::string::const_iterator> & src)
	{
		auto & it = src.first;
		auto & end = src.second;
		uint32_t len = read_len(src);
		if (!len) return std::move(std::string());
		if (std::distance(it, end) < len) {
			throw std::runtime_error("not enough");
		}
		std::string str(it, it + len);
		it += len;
		return std::move(str);
	}
	double type_interface::read_double(std::pair<std::string::const_iterator,std::string::const_iterator> & src)
	{
		auto & it = src.first;
		auto & end = src.second;
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
	void type_string::output(std::shared_ptr<file_type> & dst) const
	{
		write_string(dst, get());
	}
	void type_string::output(std::string & dst) const
	{
		write_string(dst, get());
	}
	std::shared_ptr<type_string> type_string::input(std::shared_ptr<file_type> & src)
	{
		auto strval = read_string(src);
		std::shared_ptr<type_string> result(new type_string());
		result->set(strval);
		return result;
	}
	std::shared_ptr<type_string> type_string::input(std::pair<std::string::const_iterator,std::string::const_iterator> & src)
	{
		auto strval = read_string(src);
		std::shared_ptr<type_string> result(new type_string());
		result->set(strval);
		return result;
	}
	void type_list::output(std::shared_ptr<file_type> & dst) const
	{
		write_len(dst, size());
		for (auto it = value.begin(), end = value.end(); it != end; ++it) {
			write_string(dst, *it);
		}
	}
	void type_list::output(std::string & dst) const
	{
		write_len(dst, size());
		for (auto it = value.begin(), end = value.end(); it != end; ++it) {
			write_string(dst, *it);
		}
	}
	std::shared_ptr<type_list> type_list::input(std::shared_ptr<file_type> & src)
	{
		std::shared_ptr<type_list> result(new type_list());
		std::list<std::string> & value = result->value;
		size_t & size = result->count;
		size = read_len(src);
		for (size_t i = 0, n = size; i < n; ++i) {
			value.push_back(read_string(src));
		}
		return result;
	}
	std::shared_ptr<type_list> type_list::input(std::pair<std::string::const_iterator,std::string::const_iterator> & src)
	{
		std::shared_ptr<type_list> result(new type_list());
		std::list<std::string> & value = result->value;
		size_t & size = result->count;
		size = read_len(src);
		for (size_t i = 0, n = size; i < n; ++i) {
			value.push_back(read_string(src));
		}
		return result;
	}
	void type_set::output(std::shared_ptr<file_type> & dst) const
	{
		write_len(dst, size());
		for (auto it = value.begin(), end = value.end(); it != end; ++it) {
			write_string(dst, *it);
		}
	}
	void type_set::output(std::string & dst) const
	{
		write_len(dst, size());
		for (auto it = value.begin(), end = value.end(); it != end; ++it) {
			write_string(dst, *it);
		}
	}
	std::shared_ptr<type_set> type_set::input(std::shared_ptr<file_type> & src)
	{
		std::shared_ptr<type_set> result(new type_set());
		std::set<std::string> & value = result->value;
		size_t size = read_len(src);
		for (size_t i = 0, n = size; i < n; ++i) {
			value.insert(read_string(src));
		}
		return result;
	}
	std::shared_ptr<type_set> type_set::input(std::pair<std::string::const_iterator,std::string::const_iterator> & src)
	{
		std::shared_ptr<type_set> result(new type_set());
		std::set<std::string> & value = result->value;
		size_t size = read_len(src);
		for (size_t i = 0, n = size; i < n; ++i) {
			value.insert(read_string(src));
		}
		return result;
	}
	void type_zset::output(std::shared_ptr<file_type> & dst) const
	{
		write_len(dst, size());
		for (auto it = sorted.begin(), end = sorted.end(); it != end; ++it) {
			auto & pair = *it;
			write_string(dst, pair->member);
			write_double(dst, pair->score);
		}
	}
	void type_zset::output(std::string & dst) const
	{
		write_len(dst, size());
		for (auto it = sorted.begin(), end = sorted.end(); it != end; ++it) {
			auto & pair = *it;
			write_string(dst, pair->member);
			write_double(dst, pair->score);
		}
	}
	std::shared_ptr<type_zset> type_zset::input(std::shared_ptr<file_type> & src)
	{
		size_t size = read_len(src);
		std::vector<double> scores(size);
		std::vector<std::string> members_(size);
		std::vector<std::string *> members(size);
		for (size_t i = 0, n = size; i < n; ++i) {
			auto member = read_string(src);
			auto score = read_double(src);
			scores[i] = score;
			members_[i] = member;
			members[i] = &members_[i];
		}
		std::shared_ptr<type_zset> result(new type_zset());
		result->zadd(scores, members);
		return result;
	}
	std::shared_ptr<type_zset> type_zset::input(std::pair<std::string::const_iterator,std::string::const_iterator> & src)
	{
		size_t size = read_len(src);
		std::vector<double> scores(size);
		std::vector<std::string> members_(size);
		std::vector<std::string *> members(size);
		for (size_t i = 0, n = size; i < n; ++i) {
			auto member = read_string(src);
			auto score = read_double(src);
			scores[i] = score;
			members_[i] = member;
			members[i] = &members_[i];
		}
		std::shared_ptr<type_zset> result(new type_zset());
		result->zadd(scores, members);
		return result;
	}
	void type_hash::output(std::shared_ptr<file_type> & dst) const
	{
		write_len(dst, size());
		for (auto it = value.begin(), end = value.end(); it != end; ++it) {
			auto & pair = *it;
			write_string(dst, pair.first);
			write_string(dst, pair.second);
		}
	}
	void type_hash::output(std::string & dst) const
	{
		write_len(dst, size());
		for (auto it = value.begin(), end = value.end(); it != end; ++it) {
			auto & pair = *it;
			write_string(dst, pair.first);
			write_string(dst, pair.second);
		}
	}
	std::shared_ptr<type_hash> type_hash::input(std::shared_ptr<file_type> & src)
	{
		std::shared_ptr<type_hash> result(new type_hash());
		size_t size = read_len(src);
		for (size_t i = 0, n = size; i < n; ++i) {
			auto field = read_string(src);
			auto value = read_string(src);
			result->hset(field, value);
		}
		return result;
	}
	std::shared_ptr<type_hash> type_hash::input(std::pair<std::string::const_iterator,std::string::const_iterator> & src)
	{
		std::shared_ptr<type_hash> result(new type_hash());
		size_t size = read_len(src);
		for (size_t i = 0, n = size; i < n; ++i) {
			auto field = read_string(src);
			auto value = read_string(src);
			result->hset(field, value);
		}
		return result;
	}

	void server_type::dump(std::string & dst, const std::shared_ptr<type_interface> & value)
	{
		dst.reserve(1024);
		value->output(dst);
	}
	std::shared_ptr<type_interface> server_type::restore(const std::string & src, const timeval_type & current)
	{
		std::shared_ptr<type_interface> value;
		if (src.empty()) {
			return value;
		}
		std::pair<std::string::const_iterator, std::string::const_iterator> range(src.begin(), src.end());
		switch (*range.first++) {
		case string_type:
			value = type_string::input(range);
			break;
		case list_type:
			value = type_list::input(range);
			break;
		case set_type:
			value = type_set::input(range);
			break;
		case zset_type:
			value = type_zset::input(range);
			break;
		case hash_type:
			value = type_hash::input(range);
			break;
		}
		if (std::distance(range.first, range.second) != 2 + 8) {
			throw std::runtime_error("suffix error");
		}
		uint16_t ver = *range.first++;
		ver |= (*range.first++) << 8;
		if (version < ver) {
			throw std::runtime_error("version error");
		}
		uint64_t src_crc = 0;
		for (int i = 0; i < 8; ++i) {
			uint64_t val = static_cast<uint64_t>(*range.first++) & 0xFF;
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
				type_interface::write_len(f, i);

				for (auto it = range.first; it != range.second; ++it) {
					auto & kv = *it;
					auto & key = kv.first;
					auto & expire = kv.second.first;
					auto & value = kv.second.second;
					if (expire->is_expired(current)) {
						continue;
					}
					if (expire->is_expiring()) {
						f->write8(op_expire_ms);
						f->write64(expire->at().get_ms());
					}
					f->write8(value->get_type());
					type_interface::write_string(f, key);
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
					db_index = type_interface::read_len(f);
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
						std::string key = type_interface::read_string(f);
						std::shared_ptr<type_interface> value;
						switch (op) {
						case string_type:
							value = type_string::input(f);
							break;
						case list_type:
							value = type_list::input(f);
							break;
						case set_type:
							value = type_set::input(f);
							break;
						case zset_type:
							value = type_zset::input(f);
							break;
						case hash_type:
							value = type_hash::input(f);
							break;
						}
						expire_info expire(current);
						if (expire_at) {
							expire.expire(timeval_type(expire_at / 1000, (expire_at % 1000) * 1000));
							expire_at = 0;
						}
						db->insert(key, expire, value, current);
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
