#!/bin/sh
#
# Verify the copier can copy a directory correctly on one run.  This
# used to take at least two runs, one to run mkdir and the other to
# run chmod.

. $(dirname $0)/testsuite-common.sh

setup_test

mkdir -m 755 "${srcdir}/d"
if ! $copier "${srcdir}/d/" "${dstdir}/d/" > ${logfile} ; then
    fail_test "copier returned failure when copying directories"
fi

dstmode=$(stat -f%p "${dstdir}/d" || stat -c%a "${dstdir}/d") 2>/dev/null
if [ 755 != $((dstmode % 1000)) ] ; then
    fail_test "directory created with wrong access mode"
fi

cleanup_test
