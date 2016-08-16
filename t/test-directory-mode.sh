#!/bin/sh

. $(dirname $0)/testsuite-common.sh

setup_test

mkdir "${srcdir}/subdir"

#
# Test that the initial copy task creates the directory with the
# correct mode
#
want=0770
chmod "$want" "${srcdir}/subdir"
run_daemon -1
if [ ! -d "${dstdir}/subdir" ] ; then
	fail_test "failed to copy ${srcdir}/subdir"
fi
mode=$(mode "${dstdir}/subdir")
if [ "$mode" != "$want" ] ; then
	fail_test "${srcdir}/subdir mode not copied to ${dstdir}/subdir"
fi

#
# Test that a mode change on the source directory is detected and
# propagated to the target directory, even if it already exists
#
want=0750
chmod "$want" "${srcdir}/subdir"
run_daemon -1
mode=$(mode "${dstdir}/subdir")
if [ "$mode" != "$want" ] ; then
	fail_test "${srcdir}/subdir mode not propagated to ${dstdir}/subdir"
fi

#
# Test that a mode change on the target directory is detected and
# corrected
#
chmod 0640 "${dstdir}/subdir"
run_daemon -1
mode=$(mode "${dstdir}/subdir")
if [ "$mode" != "$want" ] ; then
	fail_test "${dstdir}/subdir mode not corrected"
fi

cleanup_test
