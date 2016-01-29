#!/bin/sh

. $(dirname $0)/testsuite-common.sh

setup_test

# Scan every second, purge source after 5 seconds
run_daemon -i 1 -d 5

# Timeout for various operations
timeout=10

# wait for the pid file to appear
elapsed=0
while [ ! -s "${pidfile}" ] ; do
	[ $((elapsed+=1)) -le "${timeout}" ] ||
		fail_test "timed out waiting for pid file to appear"
	sleep 1
done
notice "pid file appeared after $elapsed seconds"

# Set up some files and directories to purge
mkdir "${srcdir}/emptydir"
mkdir "${srcdir}/subdir"
touch "${srcdir}/subdir/subfile"
sleep 5

for n in $(seq 1 5); do
	echo $n > "${srcdir}/file$n"
	sleep 1
done

# wait for source files purge
elapsed=0
while [ -e "${srcdir}/file1" ] || [ -e "${srcdir}/file2" ] || [ -e "${srcdir}/file3" ] ; do
	[ $((elapsed+=1)) -le "${timeout}" ] ||
		fail_test "timed out waiting for source files purge"
	sleep 1
done
notice "files purged after $elapsed seconds"

# kill tsdfx
pid=$(cat "${pidfile}")
expr "${pid}" : "^[1-9][0-9]*$" >/dev/null ||
	fail_test "unexpected contents in pid file"
kill "${pid}"
notice "killed daemon"

# wait for the pid file to vanish
elapsed=0
while [ -s "${pidfile}" ] ; do
	[ $((elapsed+=1)) -le "${timeout}" ] ||
		fail_test "timed out waiting for pid file to vanish"
	sleep 1
done
notice "pid file vanished after $elapsed seconds"

# Make sure too young file was not removed
if [ ! -e "${srcdir}"/file5 ] ; then
	fail_test "removed source file5 by mistake"
fi

# Make sure too directory was not removed too
if [ -d "${srcdir}"/emptydir ] ; then
	fail_test "failed to remove source directory"
fi

if ! grep -q purging "${logfile}"; then
	fail_test "unable to find purging entry in log file"
fi

cleanup_test
