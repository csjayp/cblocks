#!/bin/sh
#
set -e 

build_root=$1

init_build()
{
    chroot "${build_root}" /tmp/cblock-bootstrap.sh
    #
    # Cleanup artifacts that were in /tmp just in case subsequent stages want
    # to create directories etc (e.g.: like stage dependecies). Also we don't
    # want build artifacts hanging around in container images.
    #
    rm -fr "${build_root}"/tmp/*
}

init_build
