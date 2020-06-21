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
data_dir=$1
forge_path=$2

set -e

path_to_vol()
{
    printf "%s" "$1" | sed -E "s,^/(.*),\1,g"
}

create_forge()
{
    case $CBLOCK_FS in
    ufs)
        if [ -d "$data_dir/images/forge" ]; then
            rm -fr "$data_dir/images/forge" "$data_dir/images/forge:latest"
        fi
        mkdir -p "$data_dir/images/forge/root/tmp"
        ;;
    zfs)
        vol=$(path_to_vol "$data_dir/images/forge")
        if [ -d "$data_dir/images/forge" ]; then
            rm -fr "$data_dir/images/forge:latest"
            zfs destroy -r $vol
        fi
        zfs create $vol
        ;;
    esac
    mkdir -p "$data_dir/images/forge/root/dev"
    mkdir -p "$data_dir/images/forge/root/tmp/cblock_forge"
    gunzip -c "$forge_path" | dd bs=4096 2> "$data_dir/images/forge/TOTALS" | \
        tar -C "$data_dir/images/forge/root/" -xpf -
    ln -s "$data_dir/images/forge" "$data_dir/images/forge:latest"
}

create_forge
