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
#include <ctype.h>
#include <signal.h>

namespace rediscpp
{
	server_type::server_type()
		: shutdown(false)
		, slave(false)
		, slave_mutex(true)
		, listening_port(0)
	{
		signal(SIGPIPE, SIG_IGN);
		databases.resize(1);
		for (auto it = databases.begin(), end = databases.end(); it != end; ++it) {
			it->reset(new database_type());
		}
		build_api_map();
		bits_table.resize(256);
		for (int i = 0; i < 256; ++i) {
			int count = 0;
			for (int j = 0; j < 8; ++j) {
				if (i & (1 << j)) {
					++count;
				}
			}
			bits_table[i] = count;
		}
	}
	bool server_type::start(const std::string & hostname, const std::string & port, int threads)
	{
		std::shared_ptr<address_type> addr(new address_type);
		addr->set_hostname(hostname.c_str());
		bool is_valid;
		listening_port = atou16(port, is_valid);
		addr->set_port(listening_port);
		listening = socket_type::create(*addr);
		listening->set_reuse();
		if (!listening->bind(addr)) {
			return false;
		}
		if (!listening->listen(512)) {
			return false;
		}
		listening->set_extra(this);
		listening->set_callback(server_callback);
		listening->set_nonblocking();

		timer = timer_type::create();
		timer->set_extra(this);
		timer->set_callback(timer_callback);

		event = event_type::create();
		event->set_extra(this);
		event->set_callback(event_callback);

		poll = poll_type::create();
		poll->append(listening);
		poll->append(timer);
		poll->append(event);

		const int base_poll_count = 3;//listening, timer & event
		if (threads) {
			startup_threads(threads);
		}
		while (true) {
			try {
				process();
				//メインスレッドだけの機能
				if (shutdown) {
					if (poll->get_count() == base_poll_count) {
						lputs(__FILE__, __LINE__, info_level, "quit server, no client now");
						break;
					}
				}
				//ガベージコレクト
				{
					timeval_type tv;
					for (int i = 0, n = databases.size(); i < n; ++i) {
						auto db = writable_db(i, NULL);
						db->flush_expiring_key(tv);
					}
				}
			} catch (std::exception e) {
				lprintf(__FILE__, __LINE__, info_level, "exception:%s", e.what());
			} catch (...) {
				lputs(__FILE__, __LINE__, info_level, "exception");
			}
		}
		if (thread_pool.size()) {
		}
		return true;
	}
	void server_type::client_callback(pollable_type * p, int events)
	{
		socket_type * s = dynamic_cast<socket_type *>(p);
		if (!s) {
			return;
		}
		server_type * server = reinterpret_cast<server_type *>(s->get_extra());
		if (!server) {
			return;
		}
		server->on_client(s, events);
	}
	void server_type::server_callback(pollable_type * p, int events)
	{
		socket_type * s = dynamic_cast<socket_type *>(p);
		if (!s) {
			return;
		}
		server_type * server = reinterpret_cast<server_type *>(s->get_extra());
		if (!server) {
			return;
		}
		server->on_server(s, events);
	}
	void server_type::event_callback(pollable_type * p, int events)
	{
		event_type * e = dynamic_cast<event_type *>(p);
		if (!e) {
			return;
		}
		server_type * server = reinterpret_cast<server_type *>(e->get_extra());
		if (!server) {
			return;
		}
		server->on_event(e, events);
	}
	void server_type::timer_callback(pollable_type * p, int events)
	{
		timer_type * t = dynamic_cast<timer_type *>(p);
		if (!t) {
			return;
		}
		server_type * server = reinterpret_cast<server_type *>(t->get_extra());
		if (!server) {
			return;
		}
		server->on_timer(t, events);
	}
	void server_type::on_server(socket_type * s, int events)
	{
		std::shared_ptr<socket_type> cs = s->accept();
		if (!cs.get()) {
			return;
		}
		//新規受け付けは停止中
		if (shutdown) {
			cs->shutdown(true, true);
			cs->close();
			return;
		}
		cs->set_callback(client_callback);
		cs->set_nonblocking();
		cs->set_nodelay();
		std::shared_ptr<client_type> ct(new client_type(*this, cs, password));
		ct->set(ct);
		cs->set_extra(this);
		cs->set_extra2(ct.get());
		ct->process();
		if (cs->done()) {
			cs->close();
		} else {
			append_client(ct);
		}
	}
	void server_type::on_event(event_type * e, int events)
	{
		e->recv();
		while (true) {
			auto job = jobs.pop(0);
			if (!job) {
				break;
			}
			switch (job->type) {
			case job_type::add_type:
				append_client(job->client, true);
				break;
			case job_type::del_type:
				remove_client(job->client, true);
				break;
			case job_type::list_pushed_type:
				excecute_blocked_client(true);
				break;
			case job_type::slaveof_type:
				slaveof(job->arg1, job->arg2, true);
				break;
			case job_type::propagate_type:
				if (job->arguments.empty()) {
					propagete(job->arg1, true);
				} else {
					propagete(job->arguments, true);
				}
				break;
			}
		}
		e->mod();
	}
	void server_type::on_timer(timer_type * t, int events)
	{
		if (t->recv()) {
			excecute_blocked_client();
		}
		t->mod();
	}
	void server_type::on_client(socket_type * s, int events)
	{
		std::shared_ptr<client_type> client = reinterpret_cast<client_type *>(s->get_extra2())->get();
		if (!client) {
			lprintf(__FILE__, __LINE__, info_level, "unknown client");
			return;
		}
		if ((events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) || s->is_broken()) {
			//lprintf(__FILE__, __LINE__, info_level, "client closed");
			remove_client(client);
			return;
		}
		client->events = events;
		client->process();
		if (s->done()) {
			remove_client(client);
		} else if (!client->is_blocked()) {
			s->mod();
		}
	}
	void server_type::append_client(std::shared_ptr<client_type> client, bool now)
	{
		if (thread_pool.empty() || now) {
			if (!client->is_master() && !client->is_monitor() && !client->is_slave()) {
				poll->append(client->client);
			} else if (client->is_monitor()) {
				monitors.insert(client);
				monitoring = true;
			} else if (client->is_master()) {
				master = std::dynamic_pointer_cast<master_type>(client);
				poll->append(client->client);
			} else {
				client->client->mod();
			}
			clients[client->client.get()] = client;
		} else {
			std::shared_ptr<job_type> job(new job_type(job_type::add_type, client));
			jobs.push(job);
			event->send();
		}
	}
	void server_type::remove_client(std::shared_ptr<client_type> client, bool now)
	{
		if (thread_pool.empty() || now) {
			if (client->is_blocked()) {
				unblocked(client);
			}
			client->client->close();
			if (client->is_master()) {
				master.reset();
			}
			if (client->is_monitor()) {
				monitors.erase(client);
				monitoring = ! monitors.empty();
			}
			if (client->is_slave()) {
				mutex_locker locker(slave_mutex);
				slaves.erase(client);
			}
			clients.erase(client->client.get());
		} else {
			std::shared_ptr<job_type> job(new job_type(job_type::del_type, client));
			jobs.push(job);
			event->send();
		}
	}
	void server_type::excecute_blocked_client(bool now)
	{
		if (thread_pool.empty() || now) {
			std::set<std::shared_ptr<client_type>> clients;
			{
				mutex_locker locker(blocked_mutex);
				clients = blocked_clients;
			}
			bool set_timer = false;
			timeval_type tv;
			for (auto it = clients.begin(), end = clients.end(); it != end; ++it) {
				auto & client = *it;
				client->process();
				if (client->is_blocked()) {
					lprintf(__FILE__, __LINE__, debug_level, "still blocked");
					timeval_type ctv = client->get_blocked_till();
					if (!ctv.is_epoc()) {
						if (!set_timer || ctv < tv) {
							set_timer = true;
							if (client->current_time < ctv) {
								tv = ctv - client->current_time;
							}
						}
					}
				}
			}
			//次にタイムアウトが起きるクライアントを起こすイベントを設定する
			if (set_timer) {
				if (tv.tv_sec == 0 && tv.tv_usec == 0) {
					tv.tv_usec = 1;
				}
				timer->start(tv.tv_sec, tv.tv_usec * 1000, true);
			}
		} else {
			notify_list_pushed();
		}
	}
	void server_type::propagete(const std::string & info, bool now)
	{
		if (thread_pool.empty() || now) {
			timeval_type tv;
			for (auto it = monitors.begin(), end = monitors.end(); it != end; ++it) {
				auto & to = *it;
				to->response_status(info);
				to->flush();
				to->client->mod();
			}
		} else {
			std::shared_ptr<job_type> job(new job_type(job_type::propagate_type));
			job->arg1 = info;
			jobs.push(job);
			event->send();
		}
	}
	void server_type::propagete(const arguments_type & info, bool now)
	{
		if (thread_pool.empty() || now) {
			mutex_locker locker(slave_mutex);
			for (auto it = slaves.begin(), end = slaves.end(); it != end; ++it) {
				auto & to = *it;
				to->request(info);
				to->flush();
			}
		} else {
			std::shared_ptr<job_type> job(new job_type(job_type::propagate_type));
			job->arguments = info;
			jobs.push(job);
			event->send();
		}
	}
	void server_type::notify_list_pushed()
	{
		std::shared_ptr<job_type> job(new job_type(job_type::list_pushed_type, std::shared_ptr<client_type>()));
		jobs.push(job);
		event->send();
	}
	void server_type::build_api_map()
	{
		//connection API
		api_map["AUTH"].set(&server_type::api_auth).argc(2).type("cs");
		api_map["ECHO"].set(&server_type::api_echo).argc(2).type("cs");
		api_map["PING"].set(&server_type::api_ping);
		api_map["QUIT"].set(&server_type::api_quit);
		api_map["SELECT"].set(&server_type::api_select).argc(2).type("cd");
		//serve API
		//BGREWRITEAOF, BGSAVE, LASTSAVE, SAVE
		//CLIENT KILL, LIST, GETNAME, SETNAME
		//CONFIG GET, SET, RESETSTAT
		//DEBUG OBJECT, SETFAULT
		//INFO, SLOWLOG, 
		api_map["DBSIZE"].set(&server_type::api_dbsize);
		api_map["FLUSHALL"].set(&server_type::api_flushall).write();
		api_map["FLUSHDB"].set(&server_type::api_flushdb).write();
		api_map["SHUTDOWN"].set(&server_type::api_shutdown).argc(1,2).type("cs");
		api_map["TIME"].set(&server_type::api_time);
		api_map["SLAVEOF"].set(&server_type::api_slaveof).argc(3).type("ccc");
		api_map["SYNC"].set(&server_type::api_sync);
		api_map["REPLCONF"].set(&server_type::api_replconf).argc_gte(3).type("ccc**");
		api_map["MONITOR"].set(&server_type::api_monitor);
		//transaction API
		api_map["MULTI"].set(&server_type::api_multi);
		api_map["EXEC"].set(&server_type::api_exec);
		api_map["DISCARD"].set(&server_type::api_discard);
		api_map["WATCH"].set(&server_type::api_watch).argc_gte(2).type("ck*");
		api_map["UNWATCH"].set(&server_type::api_unwatch);
		//keys API
		//DUMP, OBJECT
		//MIGRATE, RESTORE
		api_map["KEYS"].set(&server_type::api_keys).argc(2).type("cp");
		api_map["DEL"].set(&server_type::api_del).argc_gte(2).type("ck*").write();
		api_map["EXISTS"].set(&server_type::api_exists).argc(2).type("ck");
		api_map["EXPIRE"].set(&server_type::api_expire).argc(3).type("ckt").write();
		api_map["EXPIREAT"].set(&server_type::api_expireat).argc(3).type("ckt").write();
		api_map["PERSIST"].set(&server_type::api_persist).argc(2).type("ck").write();
		api_map["TTL"].set(&server_type::api_ttl).argc(2).type("ck");
		api_map["PTTL"].set(&server_type::api_pttl).argc(2).type("ck");
		api_map["MOVE"].set(&server_type::api_move).argc(3).type("ckd").write();
		api_map["RANDOMKEY"].set(&server_type::api_randomkey);
		api_map["RENAME"].set(&server_type::api_rename).argc(3).type("ckk").write();
		api_map["RENAMENX"].set(&server_type::api_renamenx).argc(3).type("ckk").write();
		api_map["TYPE"].set(&server_type::api_type).argc(2).type("ck");
		api_map["SORT"].set(&server_type::api_sort).argc_gte(2).type("ck*").set_parser(&server_type::api_sort_store);//@note タイプが多すぎてパース出来ない
		//strings api
		api_map["GET"].set(&server_type::api_get).argc(2).type("ck");
		api_map["SET"].set(&server_type::api_set).argc(3,8).type("ckvccccc").write();
		api_map["SETEX"].set(&server_type::api_setex).argc(4).type("cktv").write();
		api_map["SETNX"].set(&server_type::api_setnx).argc(3).type("ckv").write();
		api_map["PSETEX"].set(&server_type::api_psetex).argc(4).type("cktv").write();
		api_map["STRLEN"].set(&server_type::api_strlen).argc(2).type("ck");
		api_map["APPEND"].set(&server_type::api_append).argc(3).type("ckv").write();
		api_map["GETRANGE"].set(&server_type::api_getrange).argc(4).type("cknn");
		api_map["SUBSTR"].set(&server_type::api_getrange).argc(4).type("cknn");//aka GETRANGE
		api_map["SETRANGE"].set(&server_type::api_setrange).argc(4).type("cknv").write();
		api_map["GETSET"].set(&server_type::api_getset).argc(3).type("ckv").write();
		api_map["MGET"].set(&server_type::api_mget).argc_gte(2).type("ck*");
		api_map["MSET"].set(&server_type::api_mset).argc_gte(3).type("ckv**").write();
		api_map["MSETNX"].set(&server_type::api_msetnx).argc_gte(3).type("ckv**").write();
		api_map["DECR"].set(&server_type::api_decr).argc(2).type("ck").write();
		api_map["DECRBY"].set(&server_type::api_decrby).argc(3).type("ckn").write();
		api_map["INCR"].set(&server_type::api_incr).argc(2).type("ck").write();
		api_map["INCRBY"].set(&server_type::api_incrby).argc(3).type("ckn").write();
		api_map["INCRBYFLOAT"].set(&server_type::api_incrbyfloat).argc(3).type("ckn").write();
		api_map["BITCOUNT"].set(&server_type::api_bitcount).argc(2,4).type("cknn");
		api_map["BITOP"].set(&server_type::api_bitop).argc_gte(4).type("cckk*");
		api_map["GETBIT"].set(&server_type::api_getbit).argc(3).type("ckn");
		api_map["SETBIT"].set(&server_type::api_setbit).argc(4).type("cknv").write();
		//lists api
		api_map["BLPOP"].set(&server_type::api_blpop).argc_gte(3).type("ck*t").write();
		api_map["BRPOP"].set(&server_type::api_brpop).argc_gte(3).type("ck*t").write();
		api_map["BRPOPLPUSH"].set(&server_type::api_brpoplpush).argc(4).type("ckkt").write();
		api_map["LPUSH"].set(&server_type::api_lpush).argc_gte(3).type("ckv*").write();
		api_map["RPUSH"].set(&server_type::api_rpush).argc_gte(3).type("ckv*").write();
		api_map["LPUSHX"].set(&server_type::api_lpushx).argc(3).type("ckv").write();
		api_map["RPUSHX"].set(&server_type::api_rpushx).argc(3).type("ckv").write();
		api_map["LPOP"].set(&server_type::api_lpop).argc(2).type("ck").write();
		api_map["RPOP"].set(&server_type::api_rpop).argc(2).type("ck").write();
		api_map["LINSERT"].set(&server_type::api_linsert).argc(5).type("ckccv").write();
		api_map["LINDEX"].set(&server_type::api_lindex).argc(3).type("ckn");
		api_map["LLEN"].set(&server_type::api_llen).argc(2).type("ck");
		api_map["LRANGE"].set(&server_type::api_lrange).argc(4).type("cknn");
		api_map["LREM"].set(&server_type::api_lrem).argc(4).type("cknv").write();
		api_map["LSET"].set(&server_type::api_lset).argc(4).type("cknv").write();
		api_map["LTRIM"].set(&server_type::api_ltrim).argc(4).type("cknn").write();
		api_map["RPOPLPUSH"].set(&server_type::api_rpoplpush).argc(3).type("ckk").write();
		//hashes api
		api_map["HDEL"].set(&server_type::api_hdel).argc_gte(3).type("cck*").write();
		api_map["HEXISTS"].set(&server_type::api_hexists).argc(3).type("cck");
		api_map["HGET"].set(&server_type::api_hget).argc(3).type("cck");
		api_map["HGETALL"].set(&server_type::api_hgetall).argc(2).type("cc");
		api_map["HKEYS"].set(&server_type::api_hkeys).argc(2).type("cc");
		api_map["HVALS"].set(&server_type::api_hvals).argc(2).type("cc");
		api_map["HINCRBY"].set(&server_type::api_hincrby).argc(4).type("cckn").write();
		api_map["HINCRBYFLOAT"].set(&server_type::api_hincrbyfloat).argc(4).type("cckn").write();
		api_map["HLEN"].set(&server_type::api_hlen).argc(2).type("cc");
		api_map["HMGET"].set(&server_type::api_hmget).argc_gte(3).type("cck*");
		api_map["HMSET"].set(&server_type::api_hmset).argc_gte(4).type("cckv**").write();
		api_map["HSET"].set(&server_type::api_hset).argc(4).type("cckv").write();
		api_map["HSETNX"].set(&server_type::api_hsetnx).argc(4).type("cckv").write();
		//sets api
		api_map["SADD"].set(&server_type::api_sadd).argc_gte(3).type("ckv*").write();
		api_map["SCARD"].set(&server_type::api_scard).argc(2).type("ck");
		api_map["SISMEMBER"].set(&server_type::api_sismember).argc(3).type("ckv");
		api_map["SMEMBERS"].set(&server_type::api_smembers).argc(2).type("ck");
		api_map["SMOVE"].set(&server_type::api_smove).argc(4).type("ckkv").write();
		api_map["SPOP"].set(&server_type::api_spop).argc(2).type("ck").write();
		api_map["SRANDMEMBER"].set(&server_type::api_srandmember).argc_gte(2).type("ckn");
		api_map["SREM"].set(&server_type::api_srem).argc_gte(3).type("ckv*").write();
		api_map["SDIFF"].set(&server_type::api_sdiff).argc_gte(2).type("ck*");
		api_map["SDIFFSTORE"].set(&server_type::api_sdiffstore).argc_gte(3).type("ckk*").write();
		api_map["SINTER"].set(&server_type::api_sinter).argc_gte(2).type("ck*");
		api_map["SINTERSTORE"].set(&server_type::api_sinterstore).argc_gte(3).type("ckk*").write();
		api_map["SUNION"].set(&server_type::api_sunion).argc_gte(2).type("ck*");
		api_map["SUNIONSTORE"].set(&server_type::api_sunionstore).argc_gte(3).type("ckk*").write();
		//zsets api
		api_map["ZADD"].set(&server_type::api_zadd).argc_gte(4).type("cksm**").write();
		api_map["ZCARD"].set(&server_type::api_zcard).argc(2).type("ck");
		api_map["ZCOUNT"].set(&server_type::api_zcount).argc(4).type("cknn");
		api_map["ZINCRBY"].set(&server_type::api_zincrby).argc(4).type("cknm").write();
		api_map["ZINTERSTORE"].set(&server_type::api_zinterstore).argc_gte(4).type("cknv*").write();//@note タイプが多すぎてパース出来ない
		api_map["ZUNIONSTORE"].set(&server_type::api_zunionstore).argc_gte(4).type("cknv*").write();//@note タイプが多すぎてパース出来ない
		api_map["ZRANGE"].set(&server_type::api_zrange).argc_gte(4).type("cknnc");
		api_map["ZREVRANGE"].set(&server_type::api_zrevrange).argc_gte(4).type("cknnc");
		api_map["ZRANGEBYSCORE"].set(&server_type::api_zrangebyscore).argc_gte(4).type("cknncccc");
		api_map["ZREVRANGEBYSCORE"].set(&server_type::api_zrevrangebyscore).argc_gte(4).type("cknncccc");
		api_map["ZRANK"].set(&server_type::api_zrank).argc(3).type("ckm");
		api_map["ZREVRANK"].set(&server_type::api_zrevrank).argc(3).type("ckm");
		api_map["ZREM"].set(&server_type::api_zrem).argc_gte(3).type("ckm*").write();
		api_map["ZREMRANGEBYRANK"].set(&server_type::api_zremrangebyrank).argc(4).type("cknn").write();
		api_map["ZREMRANGEBYSCORE"].set(&server_type::api_zremrangebyscore).argc(4).type("cknn").write();
		api_map["ZSCORE"].set(&server_type::api_zscore).argc(3).type("ckm");
	}
	server_type::~server_type()
	{
		shutdown_threads();
	}
	void server_type::startup_threads(int threads)
	{
		thread_pool.resize(threads);
		for (auto it = thread_pool.begin(), end = thread_pool.end(); it != end; ++it) {
			it->reset(new worker_type(*this));
			(*it)->craete();
		}
	}
	void server_type::shutdown_threads()
	{
		if (thread_pool.empty()) {
			return;
		}
		for (auto it = thread_pool.begin(), end = thread_pool.end(); it != end; ++it) {
			auto & thread = *it;
			thread->shutdown();
		}
		//@todo レベルトリガの別イベントを発生させて、終了を通知しても良い
		for (auto it = thread_pool.begin(), end = thread_pool.end(); it != end; ++it) {
			auto & thread = *it;
			thread->join();
		}
		thread_pool.clear();
	}
	void server_type::process()
	{
		try
		{
			std::vector<epoll_event> events(1);
			poll->wait(events, 1000);
			for (auto it = events.begin(), end = events.end(); it != end; ++it) {
				auto pollable = reinterpret_cast<pollable_type*>(it->data.ptr);
				if (pollable) {
					pollable->trigger(it->events);
				}
			}
		} catch (std::exception e) {
			lprintf(__FILE__, __LINE__, info_level, "exception %s", e.what());
		} catch (...) {
			lprintf(__FILE__, __LINE__, info_level, "exception");
		}
	}
	worker_type::worker_type(server_type & server_)
		: server(server_)
	{
	}
	void worker_type::run()
	{
		server.process();
	}
	database_write_locker server_type::writable_db(int index, client_type * client, bool rdlock)
	{
		if (slave && !rdlock) {
			if (client) {
				if (!client->is_master()) {
					throw std::runtime_error("ERR could not change on slave mode");
				}
			}
		}
		return database_write_locker(databases.at(index).get(), client, rdlock);
	}
	database_read_locker server_type::readable_db(int index, client_type * client)
	{
		return database_read_locker(databases.at(index).get(), client);
	}
	database_write_locker server_type::writable_db(client_type * client, bool rdlock)
	{
		return writable_db(client->get_db_index(), client, rdlock);
	}
	database_read_locker server_type::readable_db(client_type * client)
	{
		return readable_db(client->get_db_index(), client);
	}
	void server_type::blocked(std::shared_ptr<client_type> client)
	{
		if (!client) return;
		mutex_locker locker(blocked_mutex);
		blocked_clients.insert(client);
	}
	void server_type::unblocked(std::shared_ptr<client_type> client)
	{
		if (!client) return;
		mutex_locker locker(blocked_mutex);
		blocked_clients.insert(client);
	}
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
	static void write_string(std::shared_ptr<file_type> & f, const std::string & str)
	{
		write_len(f, str.size());
		f->write(str);
	}
	static void write_double(std::shared_ptr<file_type> & f, double val)
	{
		if (isnan(val)) {
			f->write8(double_nan);
		} else if (isinf(val)) {
			f->write8(val < 0 ? double_ninf : double_pinf);
		} else {
			const std::string & str = std::move(format("%.17g", val));
			f->write8(str.size());
			write_string(f, format("%.17g", val));
		}
	}
	static uint32_t read_len(std::shared_ptr<file_type> & f)
	{
		uint8_t head = f->read8();
		switch (head & 0xC0) {
		case len_6bit:
			return head & 0x3F;
		case len_14bit:
			return ((head & 0x3F) << 8) | f->read8();
		case len_32bit:
			return f->read32();
		default:
			throw std::runtime_error("length invalid");
		}
	}
	static std::string read_string(std::shared_ptr<file_type> & f)
	{
		uint32_t len = read_len(f);
		if (!len) return std::move(std::string());
		std::string str(len, '\0');
		f->read(&str[0], len);
		return std::move(str);
	}
	static double read_double(std::shared_ptr<file_type> & f)
	{
		uint8_t head = f->read8();
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
		f->read(&str[0], head);
		bool is_valid = true;
		double d = atod(str, is_valid);
		if (!is_valid) {
			throw std::runtime_error("invalid double");
		}
		return d;
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
					switch (value_type) {
					case int_type_string:
						{
							std::shared_ptr<type_string> str = std::dynamic_pointer_cast<type_string>(value);
							write_string(f, str->ref());
						}
						break;
					case int_type_list:
						{
							std::shared_ptr<type_list> list = std::dynamic_pointer_cast<type_list>(value);
							write_len(f, list->size());
							auto range = list->get_range();
							for (auto it = range.first; it != range.second; ++it) {
								auto & str = *it;
								write_string(f, str);
							}
						}
						break;
					case int_type_set:
						{
							std::shared_ptr<type_set> set = std::dynamic_pointer_cast<type_set>(value);
							write_len(f, set->size());
							auto range = set->smembers();
							for (auto it = range.first; it != range.second; ++it) {
								auto & str = *it;
								write_string(f, str);
							}
						}
						break;
					case int_type_zset:
						{
							std::shared_ptr<type_zset> zset = std::dynamic_pointer_cast<type_zset>(value);
							write_len(f, zset->size());
							auto range = zset->zrange();
							for (auto it = range.first; it != range.second; ++it) {
								auto & pair = *it;
								write_string(f, pair->member);
								write_double(f, pair->score);
							}
						}
						break;
					case int_type_hash:
						{
							std::shared_ptr<type_hash> hash = std::dynamic_pointer_cast<type_hash>(value);
							write_len(f, hash->size());
							auto range = hash->hgetall();
							for (auto it = range.first; it != range.second; ++it) {
								auto & pair = *it;
								write_string(f, pair.first);
								write_string(f, pair.second);
							}
						}
						break;
					}
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
			slave = (this->master ? true : false);
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
}
