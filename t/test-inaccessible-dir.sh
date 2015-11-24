#!/bin/sh
#
# Test error discovered 2015-11-24 and addressed in
# <URL: https://github.com/unioslo/tsdfx/pull/48 > where an
# inaccessible directory caused the list of files in a directory to be
# ignored.

. $(dirname $0)/testsuite-common.sh

setup_test

mkdir "${srcdir}/baddir1"
echo test > "${srcdir}/baddir1/test"
chmod 0 "${srcdir}/baddir1"

mkdir "${srcdir}/baddir2"
echo test > "${srcdir}/baddir2/test"
chmod 644 "${srcdir}/baddir2"

echo test2 > "${srcdir}/test2"

run_daemon

# Not expected to copy baddir1/test and baddir2/test, behind an
# inaccessible directory
for good in test2; do
	if [ ! -e "${dstdir}/${good}" ] ; then
		fail "missing: ${dstdir}/${good}"
	elif ! cmp -s "${srcdir}/${good}" "${dstdir}/${good}" ; then
		fail "incorrect: ${dstdir}/${good}"
	fi
done

cleanup_test
