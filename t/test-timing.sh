#!/bin/sh

. $(dirname $0)/testsuite-common.sh

setup_test

echo test1 > "${srcdir}/test1"
echo test2 > "${srcdir}/test2"

# the below does not work - d is created in dstdir,
# but test3 is not copied because of permission error
# mkdir ${srcdir}/d
# echo test3 > "${srcdir}/d/test3"

run_daemon

# Timeout for various operations
timeout=10

# Wait for the first scan to complete
elapsed=0
while ! grep -q tsdfx_scan_stop "${logfile}" ; do
	[ $((elapsed+=1)) -le "${timeout}" ] || 
	    fail_test "timed out waiting for first scan"
	sleep 1
done

if ! grep -q 'scanner stated 2 dir entries' "${logfile}" ; then
        fail_test "timing tests failed - unexpected number of stated files."
fi

cleanup_test
