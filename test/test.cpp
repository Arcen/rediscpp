#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

std::string string_error(int error_number)
{
	char buf[1024] = {0};
	return std::string(strerror_r(error_number, & buf[0], sizeof(buf)));
}
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
		//case EBUSY://mutex �͌��݃��b�N����Ă���B
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
		//case EINVAL://mutex ���K�؂ɏ���������Ă��Ȃ��B
		//case EDEADLK://mutex �͊��ɌĂяo���X���b�h�ɂ�胍�b�N����Ă���B (�u�G���[�������s�Ȃ��v mutexes �̂�)
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
		case EBUSY://���݃��b�N����Ă���̂� mutex ���擾�ł��Ȃ��B
			return false;
		//case EINVAL://mutex ���K�؂ɏ���������Ă��Ȃ��B
		//case EDEADLK://mutex �͊��ɌĂяo���X���b�h�ɂ�胍�b�N����Ă���B (�u�G���[�������s�Ȃ��v mutexes �̂�)
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
		//case EINVAL://mutex ���K�؂ɏ���������Ă��Ȃ��B
		//case EPERM://�Ăяo���X���b�h�� mutex �����L���Ă��Ȃ��B(�u�G���[����������v mutex �̂�)
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
	///@note mutex�̓��b�N�ς݂ŌĂԂ���
	void wait()
	{
		pthread_cond_wait(&cond, &mutex.mutex);
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
				case ESRCH://th �Ŏw�肳�ꂽ ID �ɑΉ�����X���b�h��������Ȃ������B
					joinable = false;
					break;
				case EINVAL://th �Ŏw�肳�ꂽ�X���b�h�͂��łɃf�^�b�`����Ă���B���łɕʂ̃X���b�h���X���b�h th �̏I����҂��Ă���B
				case EDEADLK://���g��҂Ƃ��Ƃ��Ă���
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
		pthread_exit(NULL);
		return NULL;
	}
	virtual void run() = 0;
};
std::string command;
class thread_tester : public thread_type
{
public:
	int index_;
	thread_tester(int index)
		: index_(index)
	{
	}
	virtual void run()
	{
		char buf[1024];
		sprintf(buf, command.c_str(), index_);
		system(buf);
		shutdown();
	}
};

int main(int argc, char *argv[])
{
	size_t thread = 1;
	for (int i = 1; i < argc; ++i) {
		if (*argv[i] == '-') {
			switch (argv[i][1]) {
			case 't':
				++i;
				if (i < argc) {
					thread = atoi(argv[i]);
				}
				continue;
			}
		}
		command += argv[i];
		command += " ";
	}
	std::vector<std::shared_ptr<thread_tester> > threads(thread);
	int index = 0;
	for (auto it = threads.begin(), end = threads.end(); it != end; ++it)
	{
		it->reset(new thread_tester(index++));
	}
	for (auto it = threads.begin(), end = threads.end(); it != end; ++it)
	{
		auto & thread = *it;
		thread->create();
	}
	for (auto it = threads.begin(), end = threads.end(); it != end; ++it)
	{
		auto & thread = *it;
		thread->join();
	}
	return 0;
}
