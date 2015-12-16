#!/bin/sh
#
# Regression test for a bug in tsdfx_scan_slurp() where, instead of
# retaining incomplete data at the end of a buffer and appending new
# data after it, it simply discards whatever is left.
#

. $(dirname $0)/testsuite-common.sh

setup_test

# content
content="the quick brown fox jumps over the squeamish ossifrage"
content_md5=$(echo $content | md5sum)

# number of files required to trigger the bug
bufsize=$(awk '/^#define.*SCAN_BUFFER_SIZE/ { print $3 }' ${source}/bin/tsdfx/scan.c)
digits=64
linelen=$((digits + 2))
linesperbuf=$((bufsize / linelen))
minfiles=$((linesperbuf))
maxfiles=$((linesperbuf + 5))

# filenames
list=${tstdir}/scanner-boundary-files
for n in $(seq 1 $maxfiles) ; do
	printf "/%0${digits}d\n" $n
done >> ${list}

while read fn ; do
	echo $content >${srcdir}${fn}
done < ${list}

run_daemon

missing=0
invalid=0
while read fn ; do
	if [ ! -f ${dstdir}${fn} ] ; then
		notice "missing: ${fn}"
		: $((missing++))
	elif [ $(md5sum ${dstdir}${fn}) != $content_md5 ] ; then
		notice "invalid contents: ${fn}"
		: $((invalid++))
	fi
done < ${list}

if [ $missing -gt 0 -o $invalid -gt 0 ] ; then
	fail "$missing missing, $invalid invalid"
fi

cleanup_test
