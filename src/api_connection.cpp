#include "server.h"
#include "log.h"

namespace rediscpp
{
	bool client_type::require_auth(const std::string & auth)
	{
		if (password.empty()) {
			return false;
		}
		if (auth == "AUTH" || auth == "QUIT") {
			return false;
		}
		return true;
	}
	bool client_type::auth(const std::string & password_)
	{
		if (password.empty()) {
			return false;
		}
		if (password == password_) {
			password.clear();
			return true;
		}
		return false;
	}
	///認証 
	///@param[in] password
	///@note Available since 1.0.0.
	bool server_type::api_auth(client_type * client)
	{
		if (!client->require_auth(std::string())) {
			throw std::runtime_error("ERR not required");
		}
		auto & arguments = client->get_arguments();
		if (arguments.size() != 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & password = arguments[1];
		if (!client->auth(password)) {
			throw std::runtime_error("ERR not match");
		}
		client->response_ok();
		return true;
	}
	///エコー 
	///@param[in] message
	///@note Available since 1.0.0.
	bool server_type::api_echo(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() < 2) {
			throw std::runtime_error("ERR syntax error");
		}
		auto & message = arguments[1];
		client->response_bulk(message);
		return true;
	}
	///Ping
	///@note Available since 1.0.0.
	bool server_type::api_ping(client_type * client)
	{
		client->response_pong();
		return true;
	}
	///終了
	///@note Available since 1.0.0.
	bool server_type::api_quit(client_type * client)
	{
		client->response_status("OK");
		client->close_after_send();
		return true;
	}
	///データベース選択
	///@param[in] index
	///@note Available since 1.0.0.
	bool server_type::api_select(client_type * client)
	{
		auto & arguments = client->get_arguments();
		if (arguments.size() != 1) {
			throw std::runtime_error("ERR syntax error");
		}
		int index = atoi(arguments[1].c_str());
		if (index < 0 || databases.size() <= index) {
			throw std::runtime_error("ERR index out of range");
		}
		client->select(index);
		client->response_status("OK");
		return true;
	}
}
