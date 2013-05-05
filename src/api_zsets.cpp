#include "server.h"

namespace rediscpp
{
	///複数のメンバーを追加
	///@note Available since 1.2.0.
	bool server_type::api_zadd(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = writable_db(client);
		auto & scores = client->get_scores();
		auto & members = client->get_members();
		std::shared_ptr<zset_type> zset = db->get_zset(key, current);
		bool created = false;
		if (!zset) {
			zset.reset(new zset_type(current));
			created = true;
		}
		std::vector<zset_type::score_type> scores_(scores.size());
		for (size_t i = 0, n = scores.size(); i < n; ++i) {
			bool is_valid = true;
			scores_[i] = atod(*scores[i], is_valid);
			if (!is_valid || isnan(scores_[i])) {
				throw std::runtime_error("ERR score is not valid number");
			}
		}
		int64_t added = zset->zadd(scores_, members);
		if (created) {
			db->replace(key, zset);
		} else {
			zset->update(current);
		}
		client->response_integer(added);
		return true;
	}
	///要素数を取得
	///@note Available since 1.2.0.
	bool server_type::api_zcard(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		std::shared_ptr<zset_type> zset = db->get_zset(key, current);
		if (!zset) {
			client->response_integer0();
			return 0;
		}
		client->response_integer(zset->size());
		return true;
	}
	///要素数を取得
	///@note Available since 2.0.0.
	bool server_type::api_zcount(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		std::string minimum = client->get_argument(2);
		std::string maximum = client->get_argument(3);
		bool inclusive_minimum = true;
		bool inclusive_maximum = true;
		if (!minimum.empty() && *minimum.begin() == '(') {
			inclusive_minimum = false;
			minimum = minimum.substr(1);
		}
		if (!maximum.empty() && *maximum.begin() == '(') {
			inclusive_maximum = false;
			maximum = maximum.substr(1);
		}
		bool is_valid = true;
		zset_type::score_type minimum_score = atod(minimum, is_valid);
		if (!is_valid || isnan(minimum_score)) {
			throw std::runtime_error("ERR min is not valid");
		}
		zset_type::score_type maximum_score = atod(maximum, is_valid);
		if (!is_valid || isnan(maximum_score)) {
			throw std::runtime_error("ERR max is not valid");
		}
		auto db = readable_db(client);
		std::shared_ptr<zset_type> zset = db->get_zset(key, current);
		if (!zset) {
			client->response_integer0();
			return 0;
		}
		size_t size = zset->zcount(minimum_score, maximum_score, inclusive_minimum, inclusive_maximum);
		client->response_integer(size);
		return true;
	}
	///スコアを加算
	///@note Available since 1.2.0.
	bool server_type::api_zincrby(client_type * client)
	{
		auto & key = client->get_argument(1);
		std::string increment = client->get_argument(2);
		auto member = client->get_argument(3);
		auto current = client->get_time();
		bool is_valid = true;
		zset_type::score_type increment_score = atod(increment, is_valid);
		if (!is_valid || isnan(increment_score)) {
			throw std::runtime_error("ERR increment is not valid");
		}
		auto db = writable_db(client);
		std::shared_ptr<zset_type> zset = db->get_zset(key, current);
		bool created = false;
		if (!zset) {
			zset.reset(new zset_type(current));
			created = true;
		}
		zset_type::score_type result_score = zset->zincrby(member, increment_score);
		if (isnan(result_score)) {
			throw std::runtime_error("ERR nan by increment");
		}
		if (created) {
			db->replace(key, zset);
		} else {
			zset->update(current);
		}
		client->response_bulk(format("%g", result_score));
		return true;
	}
	///積集合
	///@note Available since 2.0.0.
	bool server_type::api_zinterstore(client_type * client)
	{
		return api_zoperaion_internal(client, 0);
	}
	///和集合
	///@note Available since 2.0.0.
	bool server_type::api_zunionstore(client_type * client)
	{
		return api_zoperaion_internal(client, 1);
	}
	///集合演算
	///@param[in] type 0 : inter, 1 : union
	bool server_type::api_zoperaion_internal(client_type * client, int type)
	{
		auto & arguments = client->get_arguments();
		auto & destination = client->get_argument(1);
		bool is_valid;
		int64_t numkey = atoi64(client->get_argument(2), is_valid);
		size_t parsed = 3;
		if (!is_valid || numkey < 1 || arguments.size() < parsed + numkey) {
			throw std::runtime_error("ERR numkey is invalid");
		}
		std::vector<const std::string*> keys;
		keys.resize(numkey);
		for (size_t i = 0; i < keys.size(); ++i) {
			keys[i] = & arguments[i+parsed];
		}
		parsed += numkey;
		std::vector<zset_type::score_type> weights(numkey, 1.0);
		if (parsed < arguments.size()) {
			std::string keyword = arguments[parsed];
			std::transform(keyword.begin(), keyword.end(), keyword.begin(), toupper);
			if (keyword == "WEIGHTS") {
				++parsed;
				if (arguments.size() < parsed + numkey) {
					throw std::runtime_error("ERR weight is too few");
				}
				for (size_t i = 0; i < weights.size(); ++i) {
					weights[i] = atod(arguments[i+parsed], is_valid);
					if (!is_valid || isnan(weights[i])) {
						throw std::runtime_error("ERR weight is invalid");
					}
				}
				parsed += weights.size();
			}
		}
		zset_type::aggregate_types aggregate = zset_type::aggregate_sum;
		if (parsed < arguments.size()) {
			std::string keyword = arguments[parsed];
			std::transform(keyword.begin(), keyword.end(), keyword.begin(), toupper);
			if (keyword == "AGGREGATE") {
				++parsed;
				if (arguments.size() < parsed + 1) {
					throw std::runtime_error("ERR aggregate type not found");
				}
				std::string type = arguments[parsed];
				std::transform(type.begin(), type.end(), type.begin(), toupper);
				if (type == "SUM") {
				} else if (type == "MAX") {
					aggregate = zset_type::aggregate_max;
				} else if (type == "MIN") {
					aggregate = zset_type::aggregate_min;
				} else {
					throw std::runtime_error("ERR aggregate type not valid");
				}
				++parsed;
			}
		}
		if (parsed != arguments.size()) {
			throw std::runtime_error("ERR syntax error too many arguments");
		}
		auto current = client->get_time();
		auto db = writable_db(client);
		std::shared_ptr<zset_type> zset(new zset_type(current));
		bool first = true;
		auto wit = weights.begin();
		for (auto it = keys.begin(), end = keys.end(); it != end; ++it, ++wit) {
			auto & key = **it;
			std::shared_ptr<zset_type> rhs = db->get_zset(key, current);
			if (first || type) {
				first = false;
				if (rhs) {
					zset->zunion(*rhs, *wit, aggregate);
				}
			} else {
				if (rhs) {
					zset->zinter(*rhs, *wit, aggregate);
				} else {
					zset->clear();
				}
			}
		}
		db->replace(destination, zset);
		client->response_integer(zset->size());
		return true;
	}
	///要素の範囲取得(スコア順＆同じスコアはメンバーの辞書順)
	///@note Available since 1.2.0.
	bool server_type::api_zrange(client_type * client)
	{
		return api_zrange_internal(client, false);
	}
	///要素の範囲取得(逆順)
	///@note Available since 1.2.0.
	bool server_type::api_zrevrange(client_type * client)
	{
		return api_zrange_internal(client, true);
	}
	bool server_type::api_zrange_internal(client_type * client, bool rev)
	{
		auto & arguments = client->get_arguments();
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		bool withscores = false;
		if (5 == arguments.size()) {
			std::string keyword = arguments[4];
			std::transform(keyword.begin(), keyword.end(), keyword.begin(), toupper);
			if (keyword == "WITHSCORES") {
				withscores = true;
			} else {
				throw std::runtime_error("ERR syntax error");
			}
		} else if (4 != arguments.size()) {
			throw std::runtime_error("ERR syntax error");
		}
		bool is_valid;
		int64_t start = atoi64(client->get_argument(2), is_valid);
		if (!is_valid) {
			throw std::runtime_error("ERR start is not valid integer");
		}
		int64_t stop = atoi64(client->get_argument(3), is_valid);
		if (!is_valid) {
			throw std::runtime_error("ERR stop is not valid integer");
		}
		auto db = readable_db(client);
		std::shared_ptr<zset_type> zset = db->get_zset(key, current);
		if (!zset) {
			client->response_null();
			return true;
		}
		size_t size = zset->size();
		start = pos_fix(start, size);
		stop = std::min<int64_t>(size, pos_fix(stop, size) + 1);
		if (stop <= start) {
			client->response_null();
			return true;
		}
		auto range = zset->zrange(start, stop);
		size_t count = std::distance(range.first, range.second);
		if (count == 0) {
			throw std::runtime_error("ERR zset structure corrupted");
		}
		client->response_start_multi_bulk(withscores ? count * 2 : count);
		if (rev) {
			auto it = range.second;
			--it;
			while (true) {
				client->response_bulk((*it)->member);
				if (withscores) {
					client->response_bulk(format("%g", (*it)->score));
				}
				if (range.first == it) {
					break;
				}
				--it;
			}
		} else {
			for (auto it = range.first; it != range.second; ++it) {
				client->response_bulk((*it)->member);
				if (withscores) {
					client->response_bulk(format("%g", (*it)->score));
				}
			}
		}
		return true;
	}
	///要素のスコア範囲取得(スコア順＆同じスコアはメンバーの辞書順)
	///@note Available since 1.0.5.
	bool server_type::api_zrangebyscore(client_type * client)
	{
		return api_zrangebyscore_internal(client, false);
	}
	///要素のスコア範囲取得(逆順)
	///@note Available since 1.0.5.
	bool server_type::api_zrevrangebyscore(client_type * client)
	{
		return api_zrangebyscore_internal(client, true);
	}
	bool server_type::api_zrangebyscore_internal(client_type * client, bool rev)
	{
		auto & arguments = client->get_arguments();
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		std::string minimum = client->get_argument(2);
		std::string maximum = client->get_argument(3);
		bool inclusive_minimum = true;
		bool inclusive_maximum = true;
		if (!minimum.empty() && *minimum.begin() == '(') {
			inclusive_minimum = false;
			minimum = minimum.substr(1);
		}
		if (!maximum.empty() && *maximum.begin() == '(') {
			inclusive_maximum = false;
			maximum = maximum.substr(1);
		}
		bool is_valid = true;
		zset_type::score_type minimum_score = atod(minimum, is_valid);
		if (!is_valid || isnan(minimum_score)) {
			throw std::runtime_error("ERR min is not valid");
		}
		zset_type::score_type maximum_score = atod(maximum, is_valid);
		if (!is_valid || isnan(maximum_score)) {
			throw std::runtime_error("ERR max is not valid");
		}
		size_t parsed = 4;
		bool withscores = false;
		if (parsed < arguments.size()) {
			std::string keyword = arguments[parsed];
			std::transform(keyword.begin(), keyword.end(), keyword.begin(), toupper);
			if (keyword == "WITHSCORES") {
				withscores = true;
				++parsed;
			}
		}
		bool limit = false;
		int64_t limit_offset = 0;
		int64_t limit_count = 0;
		if (parsed < arguments.size()) {
			std::string keyword = arguments[parsed];
			std::transform(keyword.begin(), keyword.end(), keyword.begin(), toupper);
			if (keyword == "LIMIT") {
				limit = true;
				++parsed;
				if (arguments.size() < parsed + 2) {
					throw std::runtime_error("ERR syntax error, not found limit parameter");
				}
				limit_offset = atoi64(client->get_argument(parsed), is_valid);
				if (!is_valid || limit_offset < 0) {
					throw std::runtime_error("ERR limit offset is not valid integer");
				}
				++parsed;
				limit_count = atoi64(client->get_argument(parsed), is_valid);
				if (!is_valid || limit_count < 0) {
					throw std::runtime_error("ERR limit count is not valid integer");
				}
				++parsed;
			}
		}
		if (parsed != arguments.size()) {
			throw std::runtime_error("ERR syntax error");
		}
		auto db = readable_db(client);
		std::shared_ptr<zset_type> zset = db->get_zset(key, current);
		if (!zset) {
			client->response_null();
			return true;
		}
		auto range = zset->zrangebyscore(minimum_score, maximum_score, inclusive_minimum, inclusive_maximum);
		if (limit) {
			if (!rev) {
				for (int64_t i = 0; i < limit_offset && range.first != range.second; ++i) {
					++range.first;
				}
				auto end = range.first;
				for (int64_t i = 0; i < limit_count && end != range.second; ++i) {
					++end;
				}
				range.second = end;
			} else {
				for (int64_t i = 0; i < limit_offset && range.first != range.second; ++i) {
					--range.second;
				}
				auto begin = range.second;
				for (int64_t i = 0; i < limit_count && begin != range.first; ++i) {
					--begin;
				}
				range.first = begin;
			}
		}
		size_t count = std::distance(range.first, range.second);
		if (count == 0) {
			client->response_null();
			return true;
		}
		client->response_start_multi_bulk(withscores ? count * 2 : count);
		if (rev) {
			auto it = range.second;
			--it;
			while (true) {
				client->response_bulk((*it)->member);
				if (withscores) {
					client->response_bulk(format("%g", (*it)->score));
				}
				if (range.first == it) {
					break;
				}
				--it;
			}
		} else {
			for (auto it = range.first; it != range.second; ++it) {
				client->response_bulk((*it)->member);
				if (withscores) {
					client->response_bulk(format("%g", (*it)->score));
				}
			}
		}
		return true;
	}
	///要素の順序位置を取得
	///@note Available since 2.0.0.
	bool server_type::api_zrank(client_type * client)
	{
		return api_zrank_internal(client, false);
	}
	///要素の順序位置を取得(逆順)
	///@note Available since 2.0.0
	bool server_type::api_zrevrank(client_type * client)
	{
		return api_zrank_internal(client, true);
	}
	bool server_type::api_zrank_internal(client_type * client, bool rev)
	{
		auto & arguments = client->get_arguments();
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & member = client->get_argument(2);
		auto db = readable_db(client);
		std::shared_ptr<zset_type> zset = db->get_zset(key, current);
		if (!zset) {
			client->response_null();
			return true;
		}
		size_t rank;
		if (!zset->zrank(member, rank, rev)) {
			client->response_null();
			return true;
		}
		client->response_integer(rank);
		return true;
	}
	///スコアを取得
	///@note Available since 1.2.0.
	bool server_type::api_zscore(client_type * client)
	{
		auto & arguments = client->get_arguments();
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & member = client->get_argument(2);
		auto db = readable_db(client);
		std::shared_ptr<zset_type> zset = db->get_zset(key, current);
		if (!zset) {
			client->response_null();
			return true;
		}
		zset_type::score_type score;
		if (!zset->zscore(member, score)) {
			client->response_null();
			return true;
		}
		client->response_bulk(format("%g", score));
		return true;
	}
	///要素を削除
	///@note Available since 1.2.0.
	bool server_type::api_zrem(client_type * client)
	{
		auto & arguments = client->get_arguments();
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & members = client->get_members();
		auto db = writable_db(client);
		std::shared_ptr<zset_type> zset = db->get_zset(key, current);
		if (!zset) {
			client->response_integer0();
			return true;
		}
		size_t removed = zset->zrem(members);
		if (removed) {
			zset->update(current);
			if (zset->empty()) {
				db->erase(key, current);
			}
		}
		client->response_integer(removed);
		return true;
	}
	///ランクで要素を削除
	///@note Available since 1.2.0.
	bool server_type::api_zremrangebyrank(client_type * client)
	{
		auto & arguments = client->get_arguments();
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		bool is_valid;
		int64_t start = atoi64(client->get_argument(2), is_valid);
		if (!is_valid) {
			throw std::runtime_error("ERR start is not valid integer");
		}
		int64_t stop = atoi64(client->get_argument(3), is_valid);
		if (!is_valid) {
			throw std::runtime_error("ERR stop is not valid integer");
		}
		auto db = writable_db(client);
		std::shared_ptr<zset_type> zset = db->get_zset(key, current);
		if (!zset) {
			client->response_integer0();
			return true;
		}
		size_t size = zset->size();
		start = pos_fix(start, size);
		stop = std::min<int64_t>(size, pos_fix(stop, size) + 1);
		if (stop <= start) {
			client->response_integer0();
			return true;
		}
		auto range = zset->zrange(start, stop);
		std::list<std::string> members_;
		size_t count = 0;
		for (; range.first != range.second; ++range.first) {
			++count;
			members_.push_back((*range.first)->member);
		}
		std::vector<std::string*> members;
		members.reserve(count);
		for (auto it = members_.begin(), end = members_.end(); it != end; ++it) {
			members.push_back(&*it);
		}
		size_t removed = zset->zrem(members);
		if (removed) {
			zset->update(current);
			if (zset->empty()) {
				db->erase(key, current);
			}
		}
		client->response_integer(removed);
		return true;
	}
	///スコアの範囲で要素を削除
	///Available since 1.2.0.
	bool server_type::api_zremrangebyscore(client_type * client)
	{
		auto & arguments = client->get_arguments();
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		std::string minimum = client->get_argument(2);
		std::string maximum = client->get_argument(3);
		bool inclusive_minimum = true;
		bool inclusive_maximum = true;
		if (!minimum.empty() && *minimum.begin() == '(') {
			inclusive_minimum = false;
			minimum = minimum.substr(1);
		}
		if (!maximum.empty() && *maximum.begin() == '(') {
			inclusive_maximum = false;
			maximum = maximum.substr(1);
		}
		bool is_valid = true;
		zset_type::score_type minimum_score = atod(minimum, is_valid);
		if (!is_valid || isnan(minimum_score)) {
			throw std::runtime_error("ERR min is not valid");
		}
		zset_type::score_type maximum_score = atod(maximum, is_valid);
		if (!is_valid || isnan(maximum_score)) {
			throw std::runtime_error("ERR max is not valid");
		}
		auto db = writable_db(client);
		std::shared_ptr<zset_type> zset = db->get_zset(key, current);
		if (!zset) {
			client->response_integer0();
			return true;
		}
		auto range = zset->zrangebyscore(minimum_score, maximum_score, inclusive_minimum, inclusive_maximum);
		std::list<std::string> members_;
		size_t count = 0;
		for (; range.first != range.second; ++range.first) {
			++count;
			members_.push_back((*range.first)->member);
		}
		std::vector<std::string*> members;
		members.reserve(count);
		for (auto it = members_.begin(), end = members_.end(); it != end; ++it) {
			members.push_back(&*it);
		}
		size_t removed = zset->zrem(members);
		if (removed) {
			zset->update(current);
			if (zset->empty()) {
				db->erase(key, current);
			}
		}
		client->response_integer(removed);
		return true;
	}
};
