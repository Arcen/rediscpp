Redis CPP
=========

Redis server that implemented by CPP.

Not released yet.

No License yet.

# Detail
* multi thread with epoll
* using rwlock for database
* NO persistence

## Not support API
* CONFIG GET, SET, RESETSTAT
    * No configuration feature yet
* SLAVEOF, SYNC, MIGRATE, RESTORE
    * No client feature yet
* INFO, MONITOR, SLOWLOG, DUMP, OBJECT, DEBUG OBJECT, SEGFAULT
    * No system feature yet
* Scripting APIs
    * No lua
* Pub/Sub APIs
    * No channel message
* CLIENT KILL, LIST, GETNAME, SETNAME
    * Could not get fixed client list by multi thread
* BGREWRITEAOF, BGSAVE, LASTSAVE, SAVE
    * No support persistence

# TODO
* not implemented api
* optimize using rvalue
* file refactoring
* test code
* config file
* daemonize
* master/slave
* memory usage checking
* drop key on low memory
