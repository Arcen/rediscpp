AC_PREREQ(2.59)
AC_INIT([rediscpp], [1.0], [asakura@kbd.biglobe.ne.jp])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADERS([config.h])
AC_CHECK_LIB(pthread, pthread_create, [LIBS="$LIBS -lpthread"])

# Checks for programs.
AC_PROG_CXX
AM_PROG_CC_C_O

AC_CONFIG_FILES([Makefile src/Makefile])
AC_CONFIG_SUBDIRS([src])

AC_PROG_RANLIB
AC_OUTPUT

