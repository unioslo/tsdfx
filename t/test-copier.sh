#!/bin/sh
#
# Verify the copier can copy a directory correctly on one run.  This
# used to take at least two runs, one to run mkdir and the other to
# run chmod.

. $(dirname $0)/testsuite-common.sh

setup_test

srcmode=0750
mkdir "${srcdir}/d"
chmod "${srcmode}" "${srcdir}/d"
if ! $copier "${srcdir}/d/" "${dstdir}/d/" > ${logfile} ; then
    fail_test "copier returned failure when copying directories"
fi

dstmode=$(mode "${dstdir}/d")
if [ "${dstmode}" != "${srcmode}" ] ; then
    fail_test "directory created with wrong access mode"
fi

cleanup_test
