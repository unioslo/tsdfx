AC_PREREQ([2.64])
AC_INIT([TSD File eXchange], [1.20160201], [tsdfx-bugs@usit.uio.no],
    [tsdfx], [https://www.github.com/unioslo/tsdfx])
AC_CONFIG_SRCDIR([bin/tsdfx/tsdfx.c])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign])
AM_CONFIG_HEADER(include/config.h)

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
AC_CHECK_HEADERS([endian.h sys/endian.h sys/statvfs.h])

# functions
AC_CHECK_FUNCS([strlcat strlcpy])
AC_CHECK_FUNCS([closefrom fpurge])
AC_CHECK_FUNCS([statvfs])
AC_CHECK_FUNCS([getgroups setgroups initgroups])
AC_CHECK_FUNCS([vasprintf])

# setproctitle
AC_CHECK_HEADERS([bsd/stdlib.h bsd/unistd.h])
AC_SEARCH_LIBS([setproctitle], [bsd])
AC_CHECK_FUNCS([setproctitle_init setproctitle])

# hole detection
AC_CHECK_DECLS([SEEK_HOLE])

# options
AC_ARG_ENABLE([debug],
    AC_HELP_STRING([--enable-debug], [turn debugging macros on (default is NO)]),
    AC_DEFINE(TSDFX_DEBUG, 1, [Turn debugging macros on]))
AC_ARG_ENABLE([developer-warnings],
    AS_HELP_STRING([--enable-developer-warnings], [enable strict warnings (default is NO)]),
    [CFLAGS="${CFLAGS} -Wall -Wextra -Wcast-qual -Wshadow"])
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
    lib/Makefile
    lib/libtsd/Makefile
    bin/Makefile
    bin/tsdfx/Makefile
    libexec/Makefile
    libexec/scanner/Makefile
    libexec/copier/Makefile
    t/Makefile
])
AC_CONFIG_FILES([t/testsuite-common.sh])
AC_OUTPUT
