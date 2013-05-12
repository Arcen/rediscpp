#ifndef INCLUDE_REDIS_CPP_CLIENT_H
#define INCLUDE_REDIS_CPP_CLIENT_H

#include "network.h"
#include "thread.h"
#include "type_interface.h"

namespace rediscpp
{
	typedef std::vector<std::string> arguments_type;
	class server_type;
	struct api_info;
	class file_type;
	class client_type
	{
		friend class server_type;
		server_type & server;
		std::shared_ptr<socket_type> client;
		arguments_type arguments;
		std::vector<std::string*> keys;
		std::vector<std::string*> values;
		std::vector<std::string*> fields;
		std::vector<std::string*> members;
		std::vector<std::string*> scores;
		int argument_count;
		int argument_index;
		int argument_size;
		std::string password;
		static const int argument_is_null = -1;
		static const int argument_is_undefined = -2;
		int db_index;
		bool transaction;
		bool writing_transaction;
		bool multi_executing;
		std::list<arguments_type> transaction_arguments;
		std::set<std::tuple<std::string,int,timeval_type>> watching;
		std::vector<uint8_t> write_cache;
		timeval_type current_time;
		int events;//for thread
		bool blocked;//for list
		timeval_type blocked_till;
		std::weak_ptr<client_type> self;
		uint16_t listening_port;
		bool master;
		bool slave;
		std::shared_ptr<file_type> sending_file;
	public:
		client_type(server_type & server_, std::shared_ptr<socket_type> & client_, const std::string & password_);
		bool parse();
		const arguments_type & get_arguments() const { return arguments; }
		const std::string & get_argument(int index) const { return arguments[index]; }
		const std::vector<std::string*> & get_keys() const { return keys; }
		const std::vector<std::string*> & get_values() const { return values; }
		const std::vector<std::string*> & get_fields() const { return fields; }
		const std::vector<std::string*> & get_members() const { return members; }
		const std::vector<std::string*> & get_scores() const { return scores; }
		void response_status(const std::string & state);
		void response_error(const std::string & state);
		void response_ok();
		void response_pong();
		void response_queued();
		void response_integer(int64_t value);
		void response_integer0();
		void response_integer1();
		void response_bulk(const std::string & bulk, bool not_null = true);
		void response_null();
		void response_null_multi_bulk();
		void response_start_multi_bulk(size_t count);
		void response_raw(const std::string & raw);
		void response_file(const std::string & path);
		void flush();
		void close_after_send() { client->close_after_send(); }
		bool require_auth(const std::string & auth);
		bool auth(const std::string & password_);
		void select(int index) { db_index = index; }
		int get_db_index() { return db_index; }
		bool multi();
		bool exec();
		void discard();
		bool in_exec() const;
		bool queuing(const std::string & command, const api_info & info);
		void unwatch() { watching.clear(); }
		void watch(const std::string & key);
		std::set<std::tuple<std::string,int,timeval_type>> & get_watching() { return watching; }
		size_t get_transaction_size() { return transaction_arguments.size(); }
		bool unqueue();
		timeval_type get_time() const { return current_time; }
		void process();
		void set(std::shared_ptr<client_type> self_) { self = self_; }
		std::shared_ptr<client_type> get() { return self.lock(); }
		bool is_blocked() const { return blocked; }
		timeval_type get_blocked_till() const { return blocked_till; }
		void start_blocked(int64_t sec)
		{
			blocked = true;
			if (0 < sec) {
				blocked_till = current_time;
				blocked_till.add_msec(sec * 1000);
			} else {
				blocked_till.epoc();
			}
		}
		void end_blocked() { blocked = false; }
		bool still_block() const
		{
			return blocked && (blocked_till.is_epoc() || current_time < blocked_till);
		}
		void set_master() { master = true; }
		bool is_master() const { return master; }
		void set_slave() { slave = true; }
		bool is_slave() const { return slave; }
	private:
		void inline_command_parser(const std::string & line);
		bool parse_line(std::string & line);
		bool parse_data(std::string & data, int size);
		bool execute();
		bool execute(const api_info & info);
	};
};

#endif
