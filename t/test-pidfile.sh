#!/bin/sh

. $(dirname $0)/testsuite-common.sh

setup_test

run_daemon

# Timeout for various operations
timeout=10

# wait for the pid file to appear
elapsed=0
while [ ! -s "${pidfile}" ] ; do
	[ $((elapsed+=1)) -le "${timeout}" ] ||
		fail "timed out waiting for pid file to appear"
	sleep 1
done
notice "pid file appeared after $elapsed seconds"

# kill tsdfx
pid=$(cat "${pidfile}")
expr "${pid}" : "^[1-9][0-9]*$" >/dev/null ||
	fail "unexpected contents in pid file"
kill "${pid}"
notice "killed daemon"

# wait for the pid file to vanish
elapsed=0
while [ -s "${pidfile}" ] ; do
	[ $((elapsed+=1)) -le "${timeout}" ] ||
		fail "timed out waiting for pid file to vanish"
	sleep 1
done
notice "pid file vanished after $elapsed seconds"

cleanup_test
