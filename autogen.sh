#!/bin/sh

aclocal -I m4
autoheader
automake -a -c --foreign
autoconf
