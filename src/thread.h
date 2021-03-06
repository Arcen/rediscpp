#ifndef INCLUDE_REDIS_CPP_THREAD_H
#define INCLUDE_REDIS_CPP_THREAD_H

#include "common.h"
#include "timeval.h"
#include "log.h"
#include <pthread.h>
#include <signal.h>

namespace rediscpp
{
	class condition_type;
	class mutex_type
	{
		friend class condition_type;
		pthread_mutex_t mutex;
		mutex_type(const mutex_type & rhs);
	public:
		mutex_type(bool recursive = false)
		{
			pthread_mutexattr_t attr_;
			pthread_mutexattr_t * attr = & attr_;
			int err = pthread_mutexattr_init(&attr_);
			if (err) {
				throw std::runtime_error("pthread_mutexattr_init:" + string_error(err));
			}
			err = pthread_mutexattr_settype(attr, recursive ? PTHREAD_MUTEX_RECURSIVE_NP : PTHREAD_MUTEX_ERRORCHECK_NP);
			if (err) {
				throw std::runtime_error("pthread_mutexattr_settype:" + string_error(err));
			}
			err = pthread_mutex_init(&mutex, attr);
			if (err) {
				throw std::runtime_error("pthread_mutex_init:" + string_error(err));
			}
		}
		~mutex_type()
		{
			int err = pthread_mutex_destroy(&mutex);
			switch (err) {
			case 0:
				break;
			//case EBUSY://mutex は現在ロックされている。
			default:
				//throw std::runtime_error("pthread_mutex_destroy:" + string_error(err));
				break;
			}
		}
		void lock()
		{
			int err = pthread_mutex_lock(&mutex);
			switch (err) {
			case 0:
				return;
			//case EINVAL://mutex が適切に初期化されていない。
			//case EDEADLK://mutex は既に呼び出しスレッドによりロックされている。 (「エラー検査を行なう」 mutexes のみ)
			default:
				throw std::runtime_error("pthread_mutex_lock:" + string_error(err));
			}
		}
		bool trylock()
		{
			int err = pthread_mutex_trylock(&mutex);
			switch (err) {
			case 0:
				return true;
			case EBUSY://現在ロックされているので mutex を取得できない。
				return false;
			//case EINVAL://mutex が適切に初期化されていない。
			//case EDEADLK://mutex は既に呼び出しスレッドによりロックされている。 (「エラー検査を行なう」 mutexes のみ)
			default:
				throw std::runtime_error("pthread_mutex_trylock:" + string_error(err));
			}
		}
		void unlock()
		{
			int err = pthread_mutex_unlock(&mutex);
			switch (err) {
			case 0:
				return;
			//case EINVAL://mutex が適切に初期化されていない。
			//case EPERM://呼び出しスレッドは mutex を所有していない。(「エラーを検査する」 mutex のみ)
			default:
				throw std::runtime_error("pthread_mutex_unlock:" + string_error(err));
			}
		}
	};
	class mutex_locker
	{
		mutex_type & mutex;
	public:
		mutex_locker(mutex_type & mutex_, bool nolock = false)
			: mutex(mutex_)
		{
			if (!nolock) {
				mutex.lock();
			}
		}
		~mutex_locker()
		{
			mutex.unlock();
		}
	};
	class condition_type
	{
		pthread_cond_t cond;
		mutex_type & mutex;
		condition_type();
		condition_type(const condition_type & rhs);
	public:
		condition_type(mutex_type & mutex_)
			: mutex(mutex_)
		{
			int err = pthread_cond_init(&cond, NULL);
			if (err) {
				throw std::runtime_error("pthread_cond_init:" + string_error(err));
			}
		}
		~condition_type()
		{
			int err = pthread_cond_destroy(&cond);
		}
		///@note mutexはロック済みで呼ぶこと
		void wait()
		{
			pthread_cond_wait(&cond, &mutex.mutex);
		}
		///@note mutexはロック済みで呼ぶこと
		bool timedwait(int usec)
		{
			timeval_type tv;
			tv += timeval_type(usec / 1000000, usec % 1000000);
			timespec ts = tv.get_timespec();
			int err = pthread_cond_timedwait(&cond, &mutex.mutex, &ts);
			switch (err) {
			case 0:
				return true;
			//case EINTR://pthread_cond_timedwait がシグナルによって割り込まれた。
			//case ETIMEDOUT://条件変数が abstime で指定された時限までに送信されなかった。
			default:
				return false;
			}
		}
		void signal()
		{
			pthread_cond_signal(&cond);
		}
		void broadcast()
		{
			pthread_cond_broadcast(&cond);
		}
	};
	template<typename T>
	class sync_queue
	{
		mutex_type mutex;
		condition_type cond;
		std::queue<T> queue;
	public:
		sync_queue()
			: cond(mutex)
		{
		}
		void push(T value)
		{
			mutex_locker locker(mutex);
			queue.push(value);
			cond.broadcast();
		}
		bool empty()
		{
			mutex_locker locker(mutex);
			return queue.empty();
		}
		T pop(int usec)
		{
			mutex_locker locker(mutex);
			while (true) {
				if (queue.empty()) {
					if (usec < 0) {
						cond.wait();
						continue;
					} else if (0 < usec) {
						cond.timedwait(usec);
						usec = 0;
						continue;
					} else {
						return T();
					}
				}
				T value = queue.front();
				queue.pop();
				return value;
			}
		}
	};
	enum rwlock_types
	{
		write_lock_type,
		read_lock_type,
		no_lock_type,
	};
	class rwlock_type
	{
		pthread_rwlock_t lock;
		rwlock_type(const rwlock_type & rhs);
	public:
		rwlock_type(bool recursive = false)
		{
			int err = pthread_rwlock_init(&lock, NULL);
			if (err) {
				throw std::runtime_error("pthread_rwlock_init:" + string_error(err));
			}
		}
		~rwlock_type()
		{
			int err = pthread_rwlock_destroy(&lock);
			switch (err) {
			case 0:
				break;
			default:
				//throw std::runtime_error("pthread_rwlock_destroy:" + string_error(err));
				break;
			}
		}
		void rdlock()
		{
			int err = pthread_rwlock_rdlock(&lock);
			switch (err) {
			case 0:
				return;
			default:
				throw std::runtime_error("pthread_rwlock_rdlock:" + string_error(err));
			}
		}
		bool tryrdlock()
		{
			int err = pthread_rwlock_tryrdlock(&lock);
			switch (err) {
			case 0:
				return true;
			case EBUSY://書き込みが読み取り / 書き込みロックを保持しているか、読み取り / 書き込みロックで書き込みスレッドがブロックされているため、読み取りのための読み取り / 書き込みロックを獲得できません。
				return false;
			default:
				throw std::runtime_error("pthread_rwlock_tryrdlock:" + string_error(err));
			}
		}
		void wrlock()
		{
			int err = pthread_rwlock_wrlock(&lock);
			switch (err) {
			case 0:
				return;
			default:
				throw std::runtime_error("pthread_rwlock_wrlock:" + string_error(err));
			}
		}
		bool trywrlock()
		{
			int err = pthread_rwlock_trywrlock(&lock);
			switch (err) {
			case 0:
				return true;
			case EBUSY://読み取りまたは書き込みでロック済みのため、書き込みのための読み取り / 書き込みロックを獲得できません。
				return false;
			default:
				throw std::runtime_error("pthread_rwlock_trywrlock:" + string_error(err));
			}
		}
		void unlock()
		{
			int err = pthread_rwlock_unlock(&lock);
			switch (err) {
			case 0:
				return;
			default:
				throw std::runtime_error("pthread_rwlock_unlock:" + string_error(err));
			}
		}
	};
	class rwlock_locker
	{
		rwlock_type & rwlock;
		rwlock_types type;
	public:
		rwlock_locker(rwlock_type & rwlock_, rwlock_types type_)
			: rwlock(rwlock_)
			, type(type_)
		{
			switch (type)
			{
			case write_lock_type:
				rwlock.wrlock();
				break;
			case read_lock_type:
				rwlock.rdlock();
				break;
			}
		}
		~rwlock_locker()
		{
			switch (type)
			{
			case write_lock_type:
			case read_lock_type:
				rwlock.unlock();
				break;
			}
		}
	};
	class thread_type
	{
		pthread_t thread;
		bool joinable;
		mutex_type mutex;
		condition_type cond;
		bool shutdowning;
		bool created;
	public:
		thread_type()
			: joinable(true)
			, cond(mutex)
			, shutdowning(false)
			, created(false)
		{
		}
		bool create(bool detached = false)
		{
			if (created) {
				return false;
			}
			created = true;
			pthread_attr_t * attr = NULL;
			pthread_attr_t attr_;
			if (detached) {
				pthread_attr_init(&attr_);
				pthread_attr_setdetachstate(&attr_, PTHREAD_CREATE_DETACHED);
				attr = & attr_;
				joinable = false;
			}
			mutex_locker locker(mutex);
			int err = pthread_create(&thread, attr, start_routine, this);
			if (err) {
				throw std::runtime_error("pthread_create:" + string_error(err));
			}
			cond.wait();
			return true;
		}
		virtual ~thread_type()
		{
			join();
		}
		bool join()
		{
			if (!created) {
				return true;
			}
			if (joinable) {
				int err = pthread_join(thread, NULL);
				if (err) {
					switch (err) {
					case ESRCH://th で指定された ID に対応するスレッドが見つからなかった。
						joinable = false;
						break;
					case EINVAL://th で指定されたスレッドはすでにデタッチされている。すでに別のスレッドがスレッド th の終了を待っている。
					case EDEADLK://自身を待とうとしている
					default:
						return false;
					}
				} else {
					joinable = false;
				}
			}
			return true;
		}
		void shutdown()
		{
			mutex_locker locker(mutex);
			shutdowning = true;
		}
	private:
		static void * start_routine(void * self)
		{
			try
			{
				thread_type * my = reinterpret_cast<thread_type*>(self);
				{
					mutex_locker locker(my->mutex);
					my->cond.signal();
				}
				while (true) {
					{
						mutex_locker locker(my->mutex);
						if (my->shutdowning) {
							break;
						}
					}
					my->run();
				}
			} catch (std::exception e) {
			} catch (...) {
			}
			lprintf(__FILE__, __LINE__, error_level, "thread exit");
			pthread_exit(NULL);
			return NULL;
		}
		virtual void run() = 0;
	};
}

#endif
