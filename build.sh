#!/bin/sh
#
export PATH=/usr/local/go125/bin:$PATH

make
make install
make clean
