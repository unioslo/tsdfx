#!/bin/sh

. $(dirname $0)/testsuite-common.sh

setup_test

mkdir "${srcdir}/subdir"
chmod 0750 "${srcdir}/subdir"
mkdir "${dstdir}/subdir"
chmod 0640 "${dstdir}/subdir"

run_daemon -1

mode=$(stat --printf %a "${dstdir}/subdir" || stat -f%p "${dstdir}/subdir")
mode=$((mode % 1000))
if [ "$mode" != 750 ] ; then
	fail_test "incorrect mode on ${dstdir}/subdir: 0$mode"
fi

cleanup_test
