#!/bin/sh

. $(dirname $0)/testsuite-common.sh

setup_test

pidfile="${tstdir}/tsdfx.pid"

# Not using run_daemon() to be able to set pidfile and drop one-off run.

echo "logging to ${logfile}"
x "${tsdfx}" -l "${logfile}" -p "${pidfile}" -m "${mapfile}" -v &
pid=$!
sleep 2

if [ -f "${pidfile}" ]; then
    echo "success: found pid file ${pidfile}"
else
    fail "missing pid file ${pidfile}"
fi

kill "$(cat ${pidfile})"

sleep 2

if [ ! -f "${pidfile}" ]; then
    echo "success: pid file ${pidfile} no longer present"
else
    fail "found obsolete pid file ${pidfile}"
fi

cleanup_test
