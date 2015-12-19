#!/bin/sh

. $(dirname $0)/testsuite-common.sh

setup_test

run_daemon

# wait for the pid file to appear
limit=20
while [ ! -s "${pidfile}" ] ; do
	[ $((elapsed+=1)) -lt $limit ] ||
		fail "timed out waiting for pid file to appear"
	sleep 1
done
notice "pid file appeared after $elapsed seconds"

# kill tsdfx
kill "$(cat ${pidfile})"
notice "killed daemon"

# wait for the pid file to vanish
limit=20
while [ -s "${pidfile}" ] ; do
	[ $((elapsed+=1)) -lt $limit ] ||
		fail "timed out waiting for pid file to vanish"
	sleep 1
done
notice "pid file vanished after $elapsed seconds"

cleanup_test
