#!/bin/sh
#
# Check that small files are copied by small file copiers.

. $(dirname $0)/testsuite-common.sh

setup_test

limit=$((1024*1024))

echo test1 > "${srcdir}/smallfile"

dd bs=$limit count=1 \
    if=/dev/urandom \
    of="${srcdir}/borderfile" > /dev/null 2>&1

dd bs=$((2 * $limit)) count=1 \
    if=/dev/urandom \
    of="${srcdir}/largefile" > /dev/null 2>&1

run_daemon

for good in smallfile largefile borderfile; do
	if [ ! -e "${dstdir}/${good}" ] ; then
		fail "missing: ${dstdir}/${good}"
	elif ! cmp -s "${srcdir}/${good}" "${dstdir}/${good}" ; then
		fail "incorrect: ${dstdir}/${good}"
	fi
done

if egrep -q "Assigning .*/smallfile to copier for files size<$limit" ${logfile}; then
    notice "Correctly assigned smallfile to small file copier."
else
    fail "Did not find copier assignment for smallfile in the log."
fi

if egrep -q 'Assigning .*/borderfile to copier for files size<-1' ${logfile}; then
    notice "Correctly assigned borderfile to large file copier"
else
    fail "Did not find copier assignment for borderfile in the log."
fi

if egrep -q 'Assigning .*/largefile to copier for files size<-1' ${logfile}; then
    notice "Correctly assigned largefile to large file copier"
else
    fail "Did not find copier assignment for largefile in the log."
fi

cleanup_test
