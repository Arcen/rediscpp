#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([rediscpp], [1.0], [akira_asakura@dwango.co.jp])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADERS([config.h])

AC_CHECK_LIB(pthread, pthread_create, [LIBS="$LIBS -lpthread"])

define([build_host], esyscmd([sh -c "uname -n | tr -d '\n'"]))dnl
define([build_cpu], esyscmd([sh -c "uname -i | tr -d '\n'"]))dnl
define([build_person], esyscmd([sh -c "echo -n $USER"]))dnl
AC_DEFINE(BUILD_HOST, "build_host", [build host])
AC_DEFINE(BUILD_CPU, "build_cpu", [build cpu])
AC_DEFINE(BUILD_PERSON, "build_person", [build peson])

# Checks for programs.
AC_PROG_CXX
AM_PROG_CC_C_O

AC_CONFIG_FILES([Makefile src/Makefile])
AC_CONFIG_SUBDIRS([src])

AC_PROG_RANLIB
AC_OUTPUT

