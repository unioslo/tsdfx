AC_PREREQ([2.63])
AC_INIT([TSD File eXchange], [0.99], [d.e.smorgrav@usit.uio.no], [tsdfx], [http://www.uio.no/tjenester/it/forskning/sensitiv/])
AC_CONFIG_SRCDIR([bin/tsdfx/tsdfx.c])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign])
AM_CONFIG_HEADER([include/config.h])

# compilers
AC_LANG(C)
AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_CPP
AC_GNU_SOURCE
AC_C_CONST
AC_C_RESTRICT
AC_C_VOLATILE

# libtool
LT_PREREQ([2.2.6])
LT_INIT()

# tools
AC_PROG_INSTALL

# headers
AC_CHECK_HEADERS([endian.h sys/endian.h])

# functions
AC_CHECK_FUNCS([strlcat strlcpy])
AC_CHECK_FUNCS([closefrom])

# setproctitle
AC_CHECK_HEADERS([bsd/unistd.h])
AC_SEARCH_LIBS([setproctitle], [bsd])
AC_CHECK_FUNCS([setproctitle_init setproctitle])

# options
AC_ARG_ENABLE([debug],
    AC_HELP_STRING([--enable-debug], [turn debugging macros on (default is NO)]),
    AC_DEFINE(TSDFX_DEBUG, 1, [Turn debugging macros on]))
AC_ARG_ENABLE([developer-warnings],
    AS_HELP_STRING([--enable-developer-warnings], [enable strict warnings (default is NO)]),
    [CFLAGS="${CFLAGS} -Wall -Wextra"])
AC_ARG_ENABLE([debugging-symbols],
    AS_HELP_STRING([--enable-debugging-symbols], [enable debugging symbols (default is NO)]),
    [CFLAGS="${CFLAGS} -O0 -g -fno-inline"])
AC_ARG_ENABLE([werror],
    AS_HELP_STRING([--enable-werror], [use -Werror (default is NO)]),
    [CFLAGS="${CFLAGS} -Werror"])

# output
AC_CONFIG_FILES([
    Makefile
    include/Makefile
    bin/Makefile
    bin/tsdfx/Makefile
    lib/Makefile
    lib/libtsd/Makefile
])
AC_OUTPUT
