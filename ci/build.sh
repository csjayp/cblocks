#!/bin/sh
#
export PATH="/usr/local/go125/bin:$PATH"
export ASSUME_ALWAYS_YES=YES

pkg install go125
make
make install
make clean
