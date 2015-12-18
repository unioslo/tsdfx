#!/bin/sh

. $(dirname $0)/testsuite-common.sh

setup_test

run_daemon

# wait a while for the pid file to show up
limit=20
while [ ! -f "${pidfile}" -a 0 -ne "$limit" ] ; do
    sleep 1
    limit=$(($limit - 1))
done

if [ -f "${pidfile}" ]; then
    waittime=$((20 - $limit))
    echo "success: found pid file ${pidfile} (took $waittime seconds)"
else
    fail "missing pid file ${pidfile} (gave up after 20 seconds)"
fi

kill "$(cat ${pidfile})"

sleep 2

if [ ! -f "${pidfile}" ]; then
    echo "success: pid file ${pidfile} no longer present"
else
    fail "found obsolete pid file ${pidfile}"
fi

cleanup_test
