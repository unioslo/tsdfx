#!/bin/sh

. $(dirname $0)/testsuite-common.sh

setup_test

run_daemon

# Timeout for various operations
timeout=10

# Wait for the first scan to complete
elapsed=0
while ! grep -q tsdfx_scan_stop "${logfile}" ; do
	[ $((elapsed+=1)) -le "${timeout}" ] ||
		fail "timed out waiting for first scan"
	sleep 1
done
notice "initial scan complete after ${elapsed} seconds"
scan_stop_count=$(grep -c tsdfx_scan_stop "${logfile}")

if grep -q 'tsdfx_map_reload.*keeping' "${logfile}" ; then
	fail "map file spontaneously reloaded"
fi

echo "the quick brown fox jumps over the lazy dog" >"${srcdir}/test1"
echo "the magic words are squeamish ossifrage" >"${srcdir}/test2"

kill -HUP $(cat "${pidfile}")

# Wait for tsdfx to reload the map file
elapsed=0
while ! grep -q 'tsdfx_map_reload.*keeping' "${logfile}" ; do
	[ $((elapsed+=1)) -le "${timeout}" ] ||
		fail "timed out waiting for map reload"
	sleep 1
done
notice "map reloaded after ${elapsed} seconds"

# Wait for the copy tasks to complete
elapsed=0
while ! [ -s "${dstdir}/test1" -a -s "${dstdir}/test2" ] ; do
	[ $((elapsed+=1)) -le "${timeout}" ] ||
		fail "timed out waiting for copy"
	sleep 1
done
notice "copy complete after ${elapsed} seconds"

# Compare source and destination
for good in test1 test2 ; do
	if [ ! -e "${dstdir}/${good}" ] ; then
		fail "missing: ${dstdir}/${good}"
	elif ! cmp -s "${srcdir}/${good}" "${dstdir}/${good}" ; then
		fail "incorrect: ${dstdir}/${good}"
	fi
done

cleanup_test
