#include "server.h"
#include "log.h"

namespace rediscpp
{
	///�f�[�^�x�[�X�̃L�[���擾 
	///@note Available since 1.0.0.
	bool server_type::api_dbsize(client_type * client)
	{
		auto & db = databases[client->get_db_index()];
		client->response_integer(db.get_dbsize());
		return true;
	}
	///�f�[�^�x�[�X�̑S�L�[���� 
	///@note Available since 1.0.0.
	bool server_type::api_flushall(client_type * client)
	{
		for (auto it = databases.begin(), end = databases.end(); it != end; ++it) {
			it->clear();
		}
		client->response_ok();
		return true;
	}
	///�I�����Ă���f�[�^�x�[�X�̑S�L�[���� 
	///@note Available since 1.0.0.
	bool server_type::api_flushdb(client_type * client)
	{
		auto & db = databases[client->get_db_index()];
		db.clear();
		client->response_ok();
		return true;
	}
	///�T�[�o�̃V���b�g�_�E��
	///@note NOSAVE, SAVE�I�v�V�����͖�������
	///@note Available since 1.0.0.
	bool server_type::api_shutdown(client_type * client)
	{
		lputs(__FILE__, __LINE__, info_level, "shutdown start");
		shutdown = true;
		client->response_ok();
		return true;
	}
	///�T�[�o�̎���
	///@note Available since 2.6.0.
	///@note Time complexity: O(1)
	bool server_type::api_time(client_type * client)
	{
		timeval_type tv;
		client->response_raw(format("*2\r\n:%d\r\n:%d\r\n", tv.tv_sec, tv.tv_usec));
		return true;
	}
}