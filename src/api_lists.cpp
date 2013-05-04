#include "server.h"

namespace rediscpp
{
	///�u���b�N����������擾
	///@note Available since 2.0.0.
	bool server_type::api_blpop(client_type * client)
	{
		auto & keys = client->get_keys();
		auto current = client->get_time();
		auto db = writable_db(client);
		bool in_blocking = client->is_blocked();
		for (auto it = keys.begin(), end = keys.end(); it != end; ++it) {
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
				const std::string & result = list->lpop();
				client->response_bulk(result);
				if (in_blocking) {//�u���b�N���ɕt����
					client->end_blocked();
					unblocked(client->get());
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
	///�����֒ǉ�
	///@note Available since 1.0.0.
	bool server_type::api_lpush(client_type * client)
	{
		auto & key = client->get_argument(1);
		auto current = client->get_time();
		auto & values = client->get_values();
		auto db = writable_db(client);
		std::shared_ptr<list_type> list = db->get_list(key, current);
		if (!list) {
			list.reset(new list_type(current));
			list->lpush(values);
			db->replace(key, list);
		} else {
			list->lpush(values);
			list->update(current);
		}
		client->response_integer(list->size());
		if (client->in_exec()) {
			notify_list_pushed();
		} else {
			excecute_blocked_client();
		}
		return true;
	}
};
