#!/bin/sh
#
# Verify that the scanner max limit is working.
#

. $(dirname $0)/testsuite-common.sh

setup_test

maxfiles=5

for n in $(seq 1 $((${maxfiles} + 1)) ); do
	echo $n >${srcdir}/${n}
done

run_daemon -1 -M ${maxfiles}

if grep -q 'too many files in source' ${dstdir}/tsdfx-error.log; then
	notice "detected too many files"
else
	fail_test "failed to detect too many files"
fi

cleanup_test
