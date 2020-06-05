#!/bin/sh
#
set -e 

build_root=$1

init_build()
{
    chroot "${build_root}" /cblock-bootstrap.sh
}

init_build
