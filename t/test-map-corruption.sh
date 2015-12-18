#!/bin/sh

. $(dirname $0)/testsuite-common.sh

setup_test

run_daemon

# Wait for the first scan to complete
timeout=6
while ! grep -q tsdfx_scan_stop "${logfile}" ; do
	[ $((timeout-=1)) -gt 0 ] ||
		fail "timed out waiting for first scan"
	sleep 1
done
notice "initial scan complete"
scan_stop_count=$(grep -c tsdfx_scan_stop "${logfile}")

if grep -q 'tsdfx_map_reload.*keeping' "${logfile}" ; then
	fail "map file spontaneously reloaded"
fi

echo "the quick brown fox jumps over the lazy dog" >"${srcdir}/test1"
echo "the magic words are squeamish ossifrage" >"${srcdir}/test2"

kill -HUP $(cat "${pidfile}")

# Wait for tsdfx to reload the map file
timeout=6
while ! grep -q 'tsdfx_map_reload.*keeping' "${logfile}" ; do
	[ $((timeout-=1)) -gt 0 ] ||
		fail "timed out waiting for first scan"
	sleep 1
done
notice "map reloaded"

# Wait for the copy tasks to complete
timeout=6
while [ ! -f "${dstdir}/test1" -o ! -f "${dstdir}/test2" ] ; do
	[ $((timeout-=1)) -gt 0 ] ||
		fail "timed out waiting for copy"
	sleep 1
done
notice "copy complete"

for good in test1 test2 ; do
	if [ ! -e "${dstdir}/${good}" ] ; then
		fail "missing: ${dstdir}/${good}"
	elif ! cmp -s "${srcdir}/${good}" "${dstdir}/${good}" ; then
		fail "incorrect: ${dstdir}/${good}"
	fi
done

cleanup_test
