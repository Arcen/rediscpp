#include "server.h"
#include "client.h"
#include "master.h"
#include "log.h"
#include "file.h"
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
				if (master) {
					master->client->shutdown(true, true);
				}
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
				if (client.get() == master.get()) {
					master.reset();
				}
				slave = false;
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
		api_map["AUTH"].set(&server_type::api_auth).argc(2).type("cc");
		api_map["ECHO"].set(&server_type::api_echo).argc(2).type("cc");
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
		api_map["SHUTDOWN"].set(&server_type::api_shutdown).argc(1,2).type("cc");
		api_map["TIME"].set(&server_type::api_time);
		api_map["SLAVEOF"].set(&server_type::api_slaveof).type("ccc");
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
		api_map["KEYS"].set(&server_type::api_keys).type("cp");
		api_map["DEL"].set(&server_type::api_del).argc_gte(2).type("ck*").write();
		api_map["EXISTS"].set(&server_type::api_exists).type("ck");
		api_map["EXPIRE"].set(&server_type::api_expire).type("ckt").write();
		api_map["EXPIREAT"].set(&server_type::api_expireat).type("ckt").write();
		api_map["PERSIST"].set(&server_type::api_persist).type("ck").write();
		api_map["TTL"].set(&server_type::api_ttl).type("ck");
		api_map["PTTL"].set(&server_type::api_pttl).type("ck");
		api_map["MOVE"].set(&server_type::api_move).type("ckd").write();
		api_map["RANDOMKEY"].set(&server_type::api_randomkey);
		api_map["RENAME"].set(&server_type::api_rename).type("ckk").write();
		api_map["RENAMENX"].set(&server_type::api_renamenx).type("ckk").write();
		api_map["TYPE"].set(&server_type::api_type).type("ck");
		api_map["SORT"].set(&server_type::api_sort).argc_gte(2).type("ck*").set_parser(&server_type::api_sort_store);
		api_map["DUMP"].set(&server_type::api_dump).type("ck");
		api_map["RESTORE"].set(&server_type::api_restore).type("cktv");
		//strings api
		api_map["GET"].set(&server_type::api_get).type("ck");
		api_map["SET"].set(&server_type::api_set).argc(3,8).type("ckvccccc").write();
		api_map["SETEX"].set(&server_type::api_setex).type("cktv").write();
		api_map["SETNX"].set(&server_type::api_setnx).type("ckv").write();
		api_map["PSETEX"].set(&server_type::api_psetex).type("cktv").write();
		api_map["STRLEN"].set(&server_type::api_strlen).type("ck");
		api_map["APPEND"].set(&server_type::api_append).type("ckv").write();
		api_map["GETRANGE"].set(&server_type::api_getrange).type("cknn");
		api_map["SUBSTR"].set(&server_type::api_getrange).type("cknn");//aka GETRANGE
		api_map["SETRANGE"].set(&server_type::api_setrange).type("cknv").write();
		api_map["GETSET"].set(&server_type::api_getset).type("ckv").write();
		api_map["MGET"].set(&server_type::api_mget).argc_gte(2).type("ck*");
		api_map["MSET"].set(&server_type::api_mset).argc_gte(3).type("ckv**").write();
		api_map["MSETNX"].set(&server_type::api_msetnx).argc_gte(3).type("ckv**").write();
		api_map["DECR"].set(&server_type::api_decr).type("ck").write();
		api_map["DECRBY"].set(&server_type::api_decrby).type("ckn").write();
		api_map["INCR"].set(&server_type::api_incr).type("ck").write();
		api_map["INCRBY"].set(&server_type::api_incrby).type("ckn").write();
		api_map["INCRBYFLOAT"].set(&server_type::api_incrbyfloat).type("ckn").write();
		api_map["BITCOUNT"].set(&server_type::api_bitcount).argc(2,4).type("cknn");
		api_map["BITOP"].set(&server_type::api_bitop).argc_gte(4).type("cckk*");
		api_map["GETBIT"].set(&server_type::api_getbit).type("ckn");
		api_map["SETBIT"].set(&server_type::api_setbit).type("cknv").write();
		//lists api
		api_map["BLPOP"].set(&server_type::api_blpop).argc_gte(3).type("ck*t").write();
		api_map["BRPOP"].set(&server_type::api_brpop).argc_gte(3).type("ck*t").write();
		api_map["BRPOPLPUSH"].set(&server_type::api_brpoplpush).type("ckkt").write();
		api_map["LPUSH"].set(&server_type::api_lpush).argc_gte(3).type("ckv*").write();
		api_map["RPUSH"].set(&server_type::api_rpush).argc_gte(3).type("ckv*").write();
		api_map["LPUSHX"].set(&server_type::api_lpushx).type("ckv").write();
		api_map["RPUSHX"].set(&server_type::api_rpushx).type("ckv").write();
		api_map["LPOP"].set(&server_type::api_lpop).type("ck").write();
		api_map["RPOP"].set(&server_type::api_rpop).type("ck").write();
		api_map["LINSERT"].set(&server_type::api_linsert).type("ckccv").write();
		api_map["LINDEX"].set(&server_type::api_lindex).type("ckn");
		api_map["LLEN"].set(&server_type::api_llen).type("ck");
		api_map["LRANGE"].set(&server_type::api_lrange).type("cknn");
		api_map["LREM"].set(&server_type::api_lrem).type("cknv").write();
		api_map["LSET"].set(&server_type::api_lset).type("cknv").write();
		api_map["LTRIM"].set(&server_type::api_ltrim).type("cknn").write();
		api_map["RPOPLPUSH"].set(&server_type::api_rpoplpush).type("ckk").write();
		//hashes api
		api_map["HDEL"].set(&server_type::api_hdel).argc_gte(3).type("ckf*").write();
		api_map["HEXISTS"].set(&server_type::api_hexists).type("ckf");
		api_map["HGET"].set(&server_type::api_hget).type("ckf");
		api_map["HGETALL"].set(&server_type::api_hgetall).type("ck");
		api_map["HKEYS"].set(&server_type::api_hkeys).type("ck");
		api_map["HVALS"].set(&server_type::api_hvals).type("ck");
		api_map["HINCRBY"].set(&server_type::api_hincrby).type("ckfn").write();
		api_map["HINCRBYFLOAT"].set(&server_type::api_hincrbyfloat).type("ckfn").write();
		api_map["HLEN"].set(&server_type::api_hlen).type("ck");
		api_map["HMGET"].set(&server_type::api_hmget).argc_gte(3).type("ckf*");
		api_map["HMSET"].set(&server_type::api_hmset).argc_gte(4).type("ckfv**").write();
		api_map["HSET"].set(&server_type::api_hset).type("ckfv").write();
		api_map["HSETNX"].set(&server_type::api_hsetnx).type("ckfv").write();
		//sets api
		api_map["SADD"].set(&server_type::api_sadd).argc_gte(3).type("ckm*").write();
		api_map["SCARD"].set(&server_type::api_scard).argc(2).type("ck");
		api_map["SISMEMBER"].set(&server_type::api_sismember).argc(3).type("ckm");
		api_map["SMEMBERS"].set(&server_type::api_smembers).argc(2).type("ck");
		api_map["SMOVE"].set(&server_type::api_smove).argc(4).type("ckkm").write();
		api_map["SPOP"].set(&server_type::api_spop).argc(2).type("ck").write();
		api_map["SRANDMEMBER"].set(&server_type::api_srandmember).argc_gte(2).type("ckn");
		api_map["SREM"].set(&server_type::api_srem).argc_gte(3).type("ckm*").write();
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
		api_map["ZINTERSTORE"].set(&server_type::api_zinterstore).argc_gte(4).type("cknc*").write();//@note タイプが多すぎてパース出来ない
		api_map["ZUNIONSTORE"].set(&server_type::api_zunionstore).argc_gte(4).type("cknc*").write();//@note タイプが多すぎてパース出来ない
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
}
