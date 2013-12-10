#!/bin/sh

libtoolize --copy --force
aclocal -I m4
autoheader
automake -a -c --foreign
autoconf
