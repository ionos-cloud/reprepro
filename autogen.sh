#!/bin/sh
set -e

aclocal 
autoheader
automake -a -c
autoconf2.50

