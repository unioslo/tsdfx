#!/bin/sh

. $(dirname $0)/testsuite-common.sh

setup_test

pidfile="${tstdir}/tsdfx.pid"

# Not using run_daemon() to be able to set pidfile and drop one-off run.

echo "logging to ${logfile}"
x "${tsdfx}" -l "${logfile}" -p "${pidfile}" -m "${mapfile}" -v &
pid=$!

# wait a while for the pid file to show up
limit=20
while [ ! -f "${pidfile}" ] && [ 0 -ne "$limit" ] ; do
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
