#!/bin/sh

. $(dirname $0)/testsuite-common.sh

# write file with holes, first block 2, then block 4, finally block 1
# and then block 3.
write_holy_file() {
    filename="$1"
    dd bs=${blocksize} seek=1 count=1 conv=notrunc \
	if=/dev/urandom of="$filename" > /dev/null 2>&1
    dd bs=${blocksize} seek=3 count=1 conv=notrunc \
	if=/dev/urandom of="$filename" > /dev/null 2>&1
    sleep 3
    dd bs=${blocksize} seek=0 count=1 conv=notrunc \
	if=/dev/urandom of="$filename" > /dev/null 2>&1
    sleep 3
    dd bs=${blocksize} seek=2 count=1 conv=notrunc \
	if=/dev/urandom of="$filename" > /dev/null 2>&1
}

setup_test
set_blocksize

write_holy_file "${srcdir}/random-with-holes" &

run_daemon -1

hmd5src="$(cd "${srcdir}"; md5sum "random-with-holes")"
hmd5dst="$(cd "${dstdir}"; md5sum "random-with-holes")"
if [ "$hmd5src" != "$hmd5dst" ] ; then
    fail "File with hole changed MD5sum during transmission: $hmd5src != $hmd5dst"
else
    echo "All ok with holy file"
fi

cleanup_test
