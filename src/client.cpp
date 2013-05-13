#include "client.h"
#include "type_hash.h"
#include "type_list.h"
#include "type_set.h"
#include "type_string.h"
#include "type_zset.h"
#include "server.h"
#include "file.h"

namespace rediscpp
{
	client_type::client_type(server_type & server_, std::shared_ptr<socket_type> & client_, const std::string & password_)
		: server(server_)
		, client(client_)
		, argument_count(0)
		, argument_index(0)
		, argument_size(argument_is_undefined)
		, password(password_)
		, db_index(0)
		, transaction(false)
		, writing_transaction(false)
		, multi_executing(false)
		, current_time(0, 0)
		, blocked(false)
		, blocked_till(0, 0)
		, listening_port(0)
		, slave(false)
		, monitor(false)
		, wrote(false)
	{
		write_cache.reserve(1500);
	}
	void client_type::process()
	{
		if (is_blocked()) {
			parse();
			if (client->should_send()) {
				client->send();
			}
			if (!is_blocked()) {
				client->mod();
			}
			return;
		}
		if (events & EPOLLIN) {//recv
			//lputs(__FILE__, __LINE__, info_level, "client EPOLLIN");
			client->recv();
			if (client->should_recv()) {
				parse();
			}
			if (client->done()) {
				return;
			}
			if (client->should_send()) {
				client->send();
			}
		} else if (events & EPOLLOUT) {//send
			//lputs(__FILE__, __LINE__, info_level, "client EPOLLOUT");
			client->send();
		}
		if (is_slave()) {
			if (!client->is_sendfile() && sending_file) {
				lputs(__FILE__, __LINE__, info_level, "slave file done");
				sending_file.reset();
				flush();
				client->send();
			}
		}
	}
	void client_type::inline_command_parser(const std::string & line)
	{
		size_t end = line.size();
		for (size_t offset = 0; offset < end && offset != line.npos;) {
			size_t space = line.find(' ', offset);
			if (space != line.npos) {
				arguments.push_back(line.substr(offset, space - offset));
				offset = line.find_first_not_of(' ', space + 1);
			} else {
				arguments.push_back(line.substr(offset));
				break;
			}
		}
	}
	bool client_type::parse()
	{
		bool time_updated = false;
		try {
			while (true) {
				if (argument_count == 0) {
					std::string arg_count;
					if (!parse_line(arg_count)) {
						break;
					}
					if (arg_count.empty()) {
						continue;
					}
					char type = *arg_count.begin();
					if (type == '*') {
						argument_count = atoi(arg_count.c_str() + 1);
						argument_index = 0;
						if (argument_count <= 0) {
							lprintf(__FILE__, __LINE__, info_level, "unsupported protocol %s", arg_count.c_str());
							flush();
							return false;
						}
						arguments.clear();
						arguments.resize(argument_count);
					} else if (type == '+' || type == '-') {//response from slave
						continue;
					} else {
						inline_command_parser(arg_count);
						argument_index = argument_count = arg_count.size();
						if (!time_updated) {
							time_updated = true;
							current_time.update();
						}
						if (!execute()) {
							response_error("ERR unknown");
						}
						arguments.clear();
						argument_count = 0;
						argument_index = 0;
						argument_size = argument_is_undefined;
					}
				} else if (argument_index < argument_count) {
					if (argument_size == argument_is_undefined) {
						std::string arg_size;
						if (!parse_line(arg_size)) {
							break;
						}
						if (!arg_size.empty() && *arg_size.begin() == '$') {
							argument_size = atoi(arg_size.c_str() + 1);
							if (argument_size < -1) {
								lprintf(__FILE__, __LINE__, info_level, "unsupported protocol %s", arg_size.c_str());
								flush();
								return false;
							}
							if (argument_size < 0) {
								argument_size = argument_is_undefined;
								++argument_index;
							}
						} else {
							lprintf(__FILE__, __LINE__, info_level, "unsupported protocol %s", arg_size.c_str());
							flush();
							return false;
						}
					} else {
						std::string arg_data;
						if (!parse_data(arg_data, argument_size)) {
							break;
						}
						auto & arg = arguments[argument_index];
						arg = arg_data;
						argument_size = argument_is_undefined;
						++argument_index;
					}
				} else {
					if (!time_updated) {
						time_updated = true;
						current_time.update();
					}
					if (!execute()) {
						response_error("ERR unknown");
					}
					arguments.clear();
					argument_count = 0;
					argument_index = 0;
					argument_size = argument_is_undefined;
				}
			}
		} catch (blocked_exception e) {
		}
		flush();
		return true;
	}
	void client_type::response_status(const std::string & state)
	{
		response_raw("+" + state + "\r\n");
	}
	void client_type::response_error(const std::string & state)
	{
		response_raw("-" + state + "\r\n");
	}
	void client_type::response_ok()
	{
		response_raw("+OK\r\n");
	}
	void client_type::response_pong()
	{
		response_raw("+PONG\r\n");
	}
	void client_type::response_queued()
	{
		response_raw("+QUEUED\r\n");
	}
	void client_type::response_integer0()
	{
		response_raw(":0\r\n");
	}
	void client_type::response_integer1()
	{
		response_raw(":1\r\n");
	}
	void client_type::response_integer(int64_t value)
	{
		response_raw(format(":%"PRId64"\r\n", value));
	}
	void client_type::response_bulk(const std::string & bulk, bool not_null)
	{
		if (not_null) {
			response_raw(format("$%zd\r\n", bulk.size()));
			response_raw(bulk);
			response_raw("\r\n");
		} else {
			response_null();
		}
	}
	void client_type::response_null()
	{
		response_raw("$-1\r\n");
	}
	void client_type::response_null_multi_bulk()
	{
		response_raw("*-1\r\n");
	}
	void client_type::response_start_multi_bulk(size_t count)
	{
		response_raw(format("*%zd\r\n", count));
	}
	void client_type::response_raw(const std::string & raw)
	{
		mutex_locker locker(write_mutex);
		if (client->is_sendfile()) {
			write_cache.insert(write_cache.end(), raw.begin(), raw.end());
			return;
		}
		if (raw.size() <= write_cache.capacity() - write_cache.size()) {
			write_cache.insert(write_cache.end(), raw.begin(), raw.end());
		} else if (raw.size() <= write_cache.capacity()) {
			flush();
			write_cache.insert(write_cache.end(), raw.begin(), raw.end());
		} else {
			flush();
			client->send(raw.c_str(), raw.size());
		}
	}
	void client_type::request(const arguments_type & args)
	{
		if (args.size()) {
			lprintf(__FILE__, __LINE__, info_level, "request %s", args[0].c_str());
			response_start_multi_bulk(args.size());
			for (auto it = args.begin(), end = args.end(); it != end; ++it) {
				response_bulk(*it);
			}
		} else {
			response_null_multi_bulk();
		}
	}
	void client_type::flush()
	{
		if (!client->is_sendfile()) {
			mutex_locker locker(write_mutex);
			if (!write_cache.empty()) {
				client->send(&write_cache[0], write_cache.size());
				write_cache.clear();
			}
		}
		client->send();
	}
	bool client_type::parse_line(std::string & line)
	{
		auto & buf = client->get_recv();
		if (buf.size() < 2) {
			return false;
		}
		auto begin = buf.begin();
		auto end = buf.end();
		--end;
		auto it = std::find(begin, end, '\r');
		if (it != end) {
			line.assign(begin, it);
			std::advance(it, 2);
			buf.erase(begin, it);
			return true;
		}
		return false;
	}
	bool client_type::parse_data(std::string & data, int size)
	{
		auto & buf = client->get_recv();
		if (buf.size() < size + 2) {
			return false;
		}
		auto begin = buf.begin();
		auto end = begin;
		std::advance(end, size);
		data.assign(begin, end);
		std::advance(end, 2);
		buf.erase(begin, end);
		return true;
	}
	bool client_type::execute()
	{
		try
		{
			if (is_monitor()) {
				throw std::runtime_error("ERR monitor not accept command");
			}
			if (arguments.empty()) {
				throw std::runtime_error("ERR syntax error");
			}
			auto & command = arguments.front();
			lprintf(__FILE__, __LINE__, debug_level, "command %s", command.c_str());
			std::transform(command.begin(), command.end(), command.begin(), toupper);
			if (require_auth(command)) {
				throw std::runtime_error("NOAUTH Authentication required.");
			}
			auto it = server.api_map.find(command);
			if (it != server.api_map.end()) {
				if (queuing(command, it->second)) {
					response_queued();
					return true;
				}
				return execute(it->second);
			}
			//lprintf(__FILE__, __LINE__, info_level, "not supported command %s", command.c_str());
		} catch (blocked_exception & e) {
			throw;
		} catch (std::exception & e) {
			response_error(e.what());
			return true;
		} catch (...) {
			lputs(__FILE__, __LINE__, info_level, "unknown exception");
			return false;
		}
		return false;
	}
	bool client_type::execute(const api_info & info)
	{
		//引数確認
		if (arguments.size() < info.min_argc) {
			response_error("ERR syntax error too few arguments");
			return true;
		}
		if (info.max_argc < arguments.size()) {
			response_error("ERR syntax error too much arguments");
			return true;
		}
		try
		{
			size_t argc = arguments.size();
			size_t pattern_length = info.arg_types.size();
			size_t arg_pos = 0;
			keys.clear();
			values.clear();
			members.clear();
			fields.clear();
			scores.clear();
			keys.reserve(argc);
			values.reserve(argc);
			members.reserve(argc);
			fields.reserve(argc);
			scores.reserve(argc);
			for (; arg_pos < pattern_length && arg_pos < argc; ++arg_pos) {
				if (info.arg_types[arg_pos] == '*') {
					break;
				}
				switch (info.arg_types[arg_pos]) {
				case 'c'://command
				case 't'://time
				case 'n'://numeric
				case 'p'://pattern
					break;
				case 'k'://key
					keys.push_back(&arguments[arg_pos]);
					break;
				case 'v'://value
					values.push_back(&arguments[arg_pos]);
					break;
				case 'f'://field
					fields.push_back(&arguments[arg_pos]);
					break;
				case 'm'://member
					members.push_back(&arguments[arg_pos]);
					break;
				case 's'://scre
					scores.push_back(&arguments[arg_pos]);
					break;
				case 'd'://db index
					{
						int64_t index = atoi64(arguments[arg_pos]);
						if (index < 0 || server.databases.size() <= index) {
							throw std::runtime_error("ERR db index is wrong range");
						}
					}
					break;
				}
			}
			//後方一致
			std::list<std::string*> back_keys;
			std::list<std::string*> back_values;
			std::list<std::string*> back_fields;
			std::list<std::string*> back_members;
			std::list<std::string*> back_scores;
			if (arg_pos < argc && arg_pos < pattern_length && info.arg_types[arg_pos] == '*') {
				for (; 0 < argc && arg_pos < pattern_length; --argc, --pattern_length) {
					if (info.arg_types[pattern_length-1] == '*') {
						break;
					}
					switch (info.arg_types[pattern_length-1]) {
					case 'c'://command
					case 't'://time
					case 'n'://numeric
					case 'p'://pattern
						break;
					case 'k'://key
						back_keys.push_front(&arguments[argc-1]);
						break;
					case 'v'://value
						back_values.push_front(&arguments[argc-1]);
						break;
					case 'f'://field
						back_fields.push_back(&arguments[argc-1]);
						break;
					case 'm'://member
						back_members.push_back(&arguments[argc-1]);
						break;
					case 's'://scre
						back_scores.push_back(&arguments[argc-1]);
						break;
					case 'd'://db index
						{
							int64_t index = atoi64(arguments[argc-1]);
							if (index < 0 || server.databases.size() <= index) {
								throw std::runtime_error("ERR db index is wrong range");
							}
						}
						break;
					}
				}
			}
			if (arg_pos < argc) {
				size_t star_count = 0;
				for (size_t i = arg_pos; i < pattern_length; ++i) {
					if (info.arg_types[i] == '*') {
						++star_count;
					} else {
						throw std::runtime_error("ERR syntax error too few arguments");
					}
				}
				if (!star_count || arg_pos < star_count) {
					throw std::runtime_error("ERR command structure error");
				}
				if ((argc - arg_pos) % star_count != 0) {
					throw std::runtime_error("ERR syntax error");
				}
				for (size_t s = 0; s < star_count; ++s) {
					switch (info.arg_types[arg_pos+s-star_count]) {
					case 'k':
						for (size_t pos = arg_pos + s; pos < argc; pos += star_count) {
							keys.push_back(&arguments[pos]);
						}
						break;
					case 'v':
						for (size_t pos = arg_pos + s; pos < argc; pos += star_count) {
							values.push_back(&arguments[pos]);
						}
						break;
					case 'f':
						for (size_t pos = arg_pos + s; pos < argc; pos += star_count) {
							fields.push_back(&arguments[pos]);
						}
						break;
					case 'm':
						for (size_t pos = arg_pos + s; pos < argc; pos += star_count) {
							members.push_back(&arguments[pos]);
						}
						break;
					case 's':
						for (size_t pos = arg_pos + s; pos < argc; pos += star_count) {
							scores.push_back(&arguments[pos]);
						}
						break;
					case 'c':
						break;
					default:
						throw std::runtime_error("ERR command pattern error");
					}
				}
			}
			if (!back_keys.empty()) keys.insert(keys.end(), back_keys.begin(), back_keys.end());
			if (!back_values.empty()) values.insert(values.end(), back_values.begin(), back_values.end());
			if (!back_fields.empty()) fields.insert(fields.end(), back_fields.begin(), back_fields.end());
			if (!back_members.empty()) members.insert(members.end(), back_members.begin(), back_members.end());
			if (!back_scores.empty()) scores.insert(scores.end(), back_scores.begin(), back_scores.end());
			wrote = info.writing;
			bool result = (server.*(info.function))(this);
			if (server.monitoring) {
				std::string info = format("%d.%06d [%d %s]", current_time.tv_sec, current_time.tv_usec, db_index, client->get_peer_info().c_str());
				for (auto it = arguments.begin(), begin = it, end = arguments.end(); it != end; ++it) {
					info += format(" \"%s\"", it->c_str());
				}
				server.propagete(info);
			}
			if (wrote) {
				mutex_locker locker(server.slave_mutex);
				if (!server.slaves.empty()) {
					server.propagete(arguments);
				}
			}
			keys.clear();
			values.clear();
			fields.clear();
			members.clear();
			scores.clear();
			return result;
		} catch (...) {
			keys.clear();
			values.clear();
			fields.clear();
			members.clear();
			scores.clear();
			throw;
		}
	}
	void client_type::response_file(const std::string & path)
	{
		flush();
		sending_file = file_type::open(path, false, true);
		size_t size = sending_file->size();
		std::string header = format("$%zd\r\n", size);
		client->send(header.c_str(), header.size());
		client->sendfile(sending_file->get_fd(), size);
	}
};

