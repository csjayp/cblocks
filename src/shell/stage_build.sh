#!/bin/sh
#
# Copyright (c) 2020 Christian S.J. Peron
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
set -e 

build_root=$1

init_build()
{
    #
    # Check to see if this is a forge build. If so, change the path to the
    # interpreter. We ought to just use PATH for this.
    #
    if [ -f "${build_root}/tmp/cblock_forge/bin/sh" ]; then
        chroot "${build_root}" \
          /tmp/cblock_forge/bin/sh /tmp/cblock-bootstrap.sh
    else
        chroot "${build_root}" \
          /bin/sh /tmp/cblock-bootstrap.sh
    fi
    #
    # Cleanup artifacts that were in /tmp just in case subsequent stages want
    # to create directories etc (e.g.: like stage dependecies). Also we don't
    # want build artifacts hanging around in container images.
    #
    rm -fr "${build_root}"/tmp/*
}

init_build