#!/bin/sh
#
set -e

cd src/libprison
make
make install

cd ../../
cd src/prisond
make

cd ../../
cd src/prison
make
