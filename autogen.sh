#!/bin/sh
set -e

aclocal 
autoheader
automake -a -c
autoconf

