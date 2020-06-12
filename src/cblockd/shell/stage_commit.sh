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
build_index=$2
data_dir=$3
image_name=$4
n_stages=$5
instance="$6"
fim_spec_mode="$7"
build_tag="$8"

path_to_vol()
{
    echo -n "$1" | sed -E "s,^/(.*),\1,g"
}

commit_image()
{
    if [ -h "${build_root}/${build_index}/root/cellblock-root-ptr" ]; then
        dir=$(readlink "${build_root}/${build_index}/root/cellblock-root-ptr")
        src="${build_root}/${build_index}/root/${dir}"
        #
        # If we have root pivoting step, make sure we copy the entry point
        # and entry point args from the original root.
        #
        if [ -f "${build_root}/${build_index}/ENTRYPOINT" ]; then
            cp "${build_root}/${build_index}/ENTRYPOINT" \
                "${build_root}/${build_index}/root/${dir}"
        fi
        if [ -f "${build_root}/${build_index}/ARGS" ]; then
            cp "${build_root}/${build_index}/ARGS" \
                "${build_root}/${build_index}/root/${dir}"
        fi
    else
        rm -fr "${build_root}/${build_index}/root/tmp/*"
        src="${build_root}/${build_index}"
    fi
    if [ "${fim_spec_mode}" = "ON" ]; then
        printf "\033[1m--\033[0m %s\n" \
          "Generating cryptographic checksums for container image files"
        cd "${src}"
        mtree -c -K sha256 -p . > FIM.spec
    fi
    case $CBLOCK_FS in
    zfs)
        nvol=$(path_to_vol "${data_dir}/images/${image_name}.${instance}")
        zfs create "${nvol}"
        ;;
    ufs)
        mkdir "${data_dir}/images/${image_name}.${instance}"
        ;;
    esac
    #bytes=`du -sk "${src}" | awk '{ print $1 }'`
    lockf -k "${data_dir}/images/${image_name}.tar.zst" \
      tar -C "${src}" --exclude="/tmp" \
      --no-xattrs \
      --exclude="/dev" \
      -cf - . | \
    tar -xpf - -C "${data_dir}/images/${image_name}.${instance}"
    # NB: we need to do this atomically
    #
    if [ -h "${data_dir}/images/${image_name}:${build_tag}" ]; then
        rm "${data_dir}/images/${image_name}:${build_tag}"
    fi
    ln -s "${data_dir}/images/${image_name}.${instance}" \
        "${data_dir}/images/${image_name}:${build_tag}"
}

commit_image
