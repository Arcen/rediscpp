Redis CPP
========================
Redis server that implemented by CPP.

Not released yet.

No License yet.

# Detail
* multi thread with epoll
* using rwlock for database
* NO persistence

* Not support API
** SORT
** CLIENT KILL, LIST, GETNAME, SETNAME
** CONFIG GET, SET, RESETSTAT
** SLAVEOF, SYNC, MIGRATE, RESTORE
** INFO, MONITOR, SLOWLOG, DUMP, OBJECT
** DEBUG OBJECT, SETFAULT
** Scripting APIs
** Pub/Sub APIs
** BGREWRITEAOF, BGSAVE, LASTSAVE, SAVE

# TODO
* api SORT and etc.
* file refactoring
* test code
* config file
* daemonize
* master/slave
* memory usage checking
* drop key on low memory
