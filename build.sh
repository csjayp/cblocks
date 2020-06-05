#!/bin/sh
#
# 
set -e

cd src/libcblock
make
make install

cd ../../
cd src/cblockd
make

cd ../../
cd src/cblock
make
