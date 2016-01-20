#!/bin/sh
#
# Check that small files are copied by small file copiers.

. $(dirname $0)/testsuite-common.sh

setup_test

limit=$((1024*1024))
size_max=18446744073709551615
casefile="${tstdir}/file-class-cases"

cat >>"${casefile}" <<EOF
small 64
edge-under $((limit-1))
edge $((limit))
edge-over $((limit+1))
large $((limit*2))
EOF

while read name size ; do
	dd bs="${size}" count=1 \
	    if=/dev/urandom \
	    of="${srcdir}/${name}" > /dev/null 2>&1
done < "${casefile}"

run_daemon -1

while read name size ; do
	if [ ! -e "${dstdir}/${name}" ] ; then
		fail_test "missing: ${dstdir}/${name}"
	elif ! cmp -s "${srcdir}/${name}" "${dstdir}/${name}" ; then
		fail_test "incorrect: ${dstdir}/${name}"
	fi
	if [ "${size}" -le "${limit}" ] ; then
		class="${limit}"
	else
		class="${size_max}"
	fi
	if egrep -q "Assigning .*/${name} .* <= ${class}" "${logfile}" ; then
		notice "${name} was assigned to the correct copier"
	elif egrep -q "Assigning .*/${name} .* <= [0-9]+" "${logfile}" ; then
		fail_test "${name} was assigned to the wrong copier"
	else
		fail_test "unable to find copier assignment for ${name}"
	fi
done < "${casefile}"

cleanup_test
