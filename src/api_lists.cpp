#include "server.h"

namespace rediscpp
{
	///�u���b�N����������擾
	///@note Available since 2.0.0.
	bool server_type::api_blpop(client_type * client)
	{
		return api_bpop_internal(client, true, false);
	}
	///�u���b�N���E������擾
	///@note Available since 2.0.0.
	bool server_type::api_brpop(client_type * client)
	{
		return api_bpop_internal(client, false, false);
	}
	///�u���b�N���E������擾���A���ɒǉ�
	///@note Available since 2.2.0.
	bool server_type::api_brpoplpush(client_type * client)
	{
		return api_bpop_internal(client, false, true);
	}
	///�u���b�N���擾���A�Ώۂ�����΍��ɒǉ�����
	///@note �ǉ�����Ƃ��A�󂩃��X�g�łȂ���΁A�擾�����ɃG���[��Ԃ�
	bool server_type::api_bpop_internal(client_type * client, bool left, bool rpoplpush)
	{
		auto & keys = client->get_keys();
		auto current = client->get_time();
		auto db = writable_db(client);
		bool in_blocking = client->is_blocked();
		auto kend = (rpoplpush ? keys.begin() : keys.end());
		if (rpoplpush) {
			++kend;
		}
		for (auto it = keys.begin(), end = kend; it != end; ++it) {
			auto & key = **it;
			if (in_blocking) {
				//�u���b�N���̓��X�g�ȊO���ݒ肳��Ă��҂�
				std::shared_ptr<value_interface> value = db->get(key, current);
				if (!value || !std::dynamic_pointer_cast<list_type>(value)) {
					continue;
				}
			}
			std::shared_ptr<list_type> list = db->get_list(key, current);
			if (!list) {
				continue;
			}
			if (!list->empty()) {
				bool created = false;
				try {
					if (rpoplpush) {
						auto &destkey = **keys.rbegin();
						std::shared_ptr<list_type> dest = db->get_list(destkey, current);
						const std::string & result = list->rpop();
						if (!dest) {
							created = true;
							dest.reset(new list_type(current));
							dest->lpush(result);
							db->replace(destkey, dest);
						} else {
							dest->lpush(result);
							dest->update(current);
						}
						client->response_bulk(result);
					} else {
						const std::string & result = left ? list->lpop() : list->rpop();
						client->response_bulk(result);
					}
					if (list->empty()) {
						db->erase(key, current);
					}
				} catch (std::exception e) {//rpoplpush�ő}���悪���X�g�Ŗ����ꍇ�Ȃ�
					client->response_error(e.what());
				}
				if (in_blocking) {//�u���b�N���ɕt����
					client->end_blocked();
					unblocked(client->get());
				}
				//�V�K�ǉ��̏ꍇ�͒ʒm����
				if (created) {
					if (client->in_exec()) {
						notify_list_pushed();
					} else {
						excecute_blocked_client();
					}
				}
				return true;
			}
		}
		if (client->in_exec()) {//��u���b�N
			client->response_null();
			return true;
		}
		if (in_blocking && !client->still_block()) {//�^�C���A�E�g
			client->end_blocked();
			client->response_null();
			return true;
		}
		if (!in_blocking) {//�ŏ��̃u���b�N���N�����ꍇ
			auto & arguments = client->get_arguments();
			int64_t timeout = atoi64(arguments.back());
			client->start_blocked(timeout);
			timer->insert(timeout, 0);//�^�C�}�[��ǉ�
			//�u���b�N���X�g�ɒǉ�
			blocked(client->get());
		}
		throw blocked_exception("blocking");
	}
	///�擾���A�Ώۂ�����΍��ɒǉ�����
	///@note �ǉ�����Ƃ��A�󂩃��X�g�łȂ���΁A�擾�����ɃG���[��Ԃ�
	bool server_type::api_rpoplpush(client_type * client)
	{
		auto current = client->get_time();
		auto db = writable_db(client);
		bool in_blocking = client->is_blocked();
		auto & key = client->get_argument(1);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list || list->empty()) {
			client->response_null();
			return true;
		}
		auto & destkey = client->get_argument(2);
		std::shared_ptr<list_type> dest = db->get_list(destkey, current);
		const std::string & result = list->rpop();
		bool created = false;
		if (!dest) {
			created = true;
			dest.reset(new list_type(current));
			dest->lpush(result);
			db->replace(destkey, dest);
		} else {
			dest->lpush(result);
			dest->update(current);
		}
		if (list->empty()) {
			db->erase(key, current);
		}
		client->response_bulk(result);
		//�V�K�ǉ��̏ꍇ�͒ʒm����
		if (created) {
			if (client->in_exec()) {
				notify_list_pushed();
			} else {
				excecute_blocked_client();
			}
		}
		return true;
	}
	///�����֒ǉ�
	///@note Available since 1.0.0.
	bool server_type::api_lpush(client_type * client)
	{
		return api_lpush_internal(client, true, false);
	}
	///�����փ��X�g������Βǉ�
	///@note Available since 2.2.0.
	bool server_type::api_lpushx(client_type * client)
	{
		return api_lpush_internal(client, true, true);
	}
	///�E���֒ǉ�
	///@note Available since 1.0.0.
	bool server_type::api_rpush(client_type * client)
	{
		return api_lpush_internal(client, false, false);
	}
	///�E���փ��X�g������Βǉ�
	///@note Available since 2.2.0.
	bool server_type::api_rpushx(client_type * client)
	{
		return api_lpush_internal(client, false, true);
	}
	///���X�g�֒ǉ�
	bool server_type::api_lpush_internal(client_type * client, bool left, bool exist)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & values = client->get_values();
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		bool created = false;
		if (!list) {
			if (exist) {
				client->response_integer0();
				return true;
			}
			list.reset(new list_type(current));
			if (left) {
				list->lpush(values);
			} else {
				list->rpush(values);
			}
			db->replace(key, list);
			created = true;
		} else {
			if (left) {
				list->lpush(values);
			} else {
				list->rpush(values);
			}
			list->update(current);
		}
		client->response_integer(list->size());
		//�V�K�ǉ����̒ʒm
		if (created) {
			if (client->in_exec()) {
				notify_list_pushed();
			} else {
				excecute_blocked_client();
			}
		}
		return true;
	}
	///���X�g�̎w��̒l������΁A���̑O�����֒ǉ�
	bool server_type::api_linsert(client_type * client)
	{
		auto side = client->get_argument(2);
		std::transform(side.begin(), side.end(), side.begin(), toupper);
		auto before = (side == "BEFORE");
		if (!before && side != "AFTER") {
			throw std::runtime_error("ERR syntax error");
		}

		auto & key = client->get_argument(1);
		auto & pivot = client->get_argument(3);
		auto & value = client->get_argument(4);
		auto current = client->get_time();
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list || !list->linsert(pivot, value, before)) {
			client->response_integer(0);
			return true;
		}
		list->update(current);
		client->response_integer(list->size());
		return true;
	}
	///������擾
	///@note Available since 1.0.0.
	bool server_type::api_lpop(client_type * client)
	{
		return api_lpop_internal(client, true);
	}
	///�E����擾
	///@note Available since 1.0.0.
	bool server_type::api_rpop(client_type * client)
	{
		return api_lpop_internal(client, false);
	}
	///���X�g����擾
	bool server_type::api_lpop_internal(client_type * client, bool left)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_null();
			return true;
		}
		const std::string & value = left ? list->lpop() : list->rpop();
		if (list->empty()) {
			db->erase(key, current);
		} else {
			list->update(current);
		}
		client->response_bulk(value);
		return true;
	}
	///���X�g�̒���
	///@note Available since 1.0.0.
	bool server_type::api_llen(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_integer(0);
			return true;
		}
		client->response_integer(list->size());
		return true;
	}
	///�w��ʒu�̒l���擾
	///@note Available since 1.0.0.
	bool server_type::api_lindex(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_null();
			return true;
		}
		int64_t index = atoi64(client->get_argument(2));
		if (index < 0) {
			index += list->size();
		}
		if (index < 0 || list->size() <= index) {
			client->response_null();
			return true;
		}
		auto it = list->get_it(index);
		client->response_bulk(*it);
		return true;
	}
	///�w��ʒu�͈̔͂��擾
	///@note Available since 1.0.0.
	bool server_type::api_lrange(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = readable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_null_multi_bulk();
			return true;
		}
		int64_t start = pos_fix(atoi64(client->get_argument(2)), list->size());
		int64_t end = std::min<int64_t>(list->size(), pos_fix(atoi64(client->get_argument(3)), list->size()) + 1);
		if (end <= start) {
			client->response_null_multi_bulk();
			return true;
		}
		size_t count = end - start;
		auto range = list->get_range(start, end);
		client->response_start_multi_bulk(count);
		for (auto it = range.first, end = range.second; it != end; ++it) {
			client->response_bulk(*it);
		}
		return true;
	}
	///���X�g����w��̒l���폜
	bool server_type::api_lrem(client_type * client)
	{
		auto & key = client->get_argument(1);
		int64_t count = atoi64(client->get_argument(2));
		auto & value = client->get_argument(3);
		auto current = client->get_time();
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_integer0();
			return true;
		}
		int64_t removed = list->lrem(count, value);
		if (list->empty()) {
			db->erase(key, current);
		} else {
			list->update(current);
		}
		client->response_integer(removed);
		return true;
	}
	///���X�g�̗L���͈͂��w�肵�Ďc����폜����
	bool server_type::api_ltrim(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_ok();
			return true;
		}
		int64_t start = pos_fix(atoi64(client->get_argument(2)), list->size());
		int64_t end = std::min<int64_t>(list->size(), pos_fix(atoi64(client->get_argument(3)), list->size()) + 1);
		list->trim(start, end);
		if (list->empty()) {
			db->erase(key, current);
		} else {
			list->update(current);
		}
		client->response_ok();
		return true;
	}
	///���X�g�̎w��ʒu�̒l��ύX����
	bool server_type::api_lset(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & value = client->get_argument(3);
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			client->response_ok();
			return true;
		}
		int64_t index = pos_fix(atoi64(client->get_argument(2)), list->size());
		if (!list->set(index, value)) {
			throw std::runtime_error("ERR index out of range");
		}
		list->update(current);
		client->response_ok();
		return true;
	}
};
