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
* MIGRATE, RESTORE, DUMP
    * No another db feature yet
* INFO, SLOWLOG
    * No system feature yet
* DEBUG OBJECT, OBJECT
    * No some structure
* DEBUG SEGFAULT
    * No need
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
* master/slave test
* memory usage checking
* drop key on low memory
