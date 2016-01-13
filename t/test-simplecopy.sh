#!/bin/sh

. $(dirname $0)/testsuite-common.sh

setup_test

# How large should the file with random data be
randomsize=5000 # kiB

# Try to trigger scanner problem discovered 2015-10-28
echo "badfile, should be ignored" > "${srcdir}/$(printf "\002")"

echo test1 > "${srcdir}/test1"
echo 'test(2)' > "${srcdir}/test (2).txt"
echo test2 > "${srcdir}/test2"

dd bs=1k count=${randomsize} \
    if=/dev/urandom \
    of="${srcdir}/${randomsize}krandom" > /dev/null 2>&1

md5start=$(cd ${srcdir}; md5sum ${randomsize}krandom)

run_daemon -1

for good in test1 test2 ; do
	if [ ! -e "${dstdir}/${good}" ] ; then
		fail "missing: ${dstdir}/${good}"
	elif ! cmp -s "${srcdir}/${good}" "${dstdir}/${good}" ; then
		fail "incorrect: ${dstdir}/${good}"
	fi
done

for bad in "test (2).txt" ; do
	if [ -e "${bad}" ] ; then
		fail "should not exist: ${bad}"
	fi
done

md5end=$(cd ${dstdir}; md5sum ${randomsize}krandom)
if [ "$md5start" != "$md5end" ]; then
    fail "${randomsize}krandom file changed from source to destination"
fi

# Make sure failed copy of "test (2).txt" and others are reported as
# errors in destination directory.
if [ ! -e "${dstdir}/tsdfx-error.log" ] ; then
    fail "missing ${dstdir}/tsdfx-error.log"
fi

cleanup_test
