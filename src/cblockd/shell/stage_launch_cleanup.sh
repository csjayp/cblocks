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
data_root="$1"
instance="$2"
type=$3

path_to_vol()
{
    echo -n "$1" | sed -E "s,^/(.*),\1,g"
}

network_cleanup()
{
    while read ln; do
        ins=$(echo "$ln" | awk -F: '{ print $2 }')
        if [ "$ins" != "$instance" ]; then
            continue
        fi
        type=$(echo "$ln" | awk -F: '{ print $1 }')
        case $type in
        nat)
            ip=$(echo "$ln" | awk -F: '{ print $3 }')
            pfctl -a cblock-nat/"${instance}" -Fa
            pfctl -a cblock-rdr/"${instance}" -Fa
            ifconfig cblock0 "${ip}"/32 delete
            ;;
        esac
        break
    done < "$data_root"/networks/cur
}

kill_jail()
{
    pattern=$(echo "$instance" | awk '{ printf("%.10s", $1) }')
    jail_id=$(jls | grep -F "$pattern" | awk '{ print $1 }')
    if [ "$jail_id" ]; then
        jail -r "$jail_id"
    fi
}

umount_reverse_order()
{
    for fs in $(mount -p | awk '{ print $2 }' | grep -F "${instance}" | tail -r); do
        umount -f "$fs"
    done
}

zfs_lookup_origin()
{
    zfs get origin "$1" | grep "^$1" | awk '{ print $3 }'
}

cleanup()
{
    case "$type" in
    build)
        #
        # Clean up build contexts and bootstrap scripts
        #
        rm -fr "${data_root}/instances/${instance}.tar.gz"
        rm -fr "${data_root}/instances/${instance}.*.sh"
        rm -fr "${data_root}/instances/${instance}/images"
        stage_list=$(echo "${data_root}"/instances/"${instance}"/[0-9]*)
        for d in $stage_list; do
            umount -f "${d}/root/dev"
            case $CBLOCK_FS in
            fuse-unionfs)
                umount -f "${d}/root"
                rm -fr "${data_root}/unions/${instance}/*"
                ;;
            ufs)
                umount -f "${d}/root"
                ;;
            esac
        done
        case $CBLOCK_FS in
        zfs)
            build_root_vol=$(path_to_vol "${data_root}/instances/${instance}")
            # Recursively remove the ZFS datasets (snapshots and file systems)
            zfs destroy -r "${build_root_vol}"
            ;;
        esac
        ;;
    regular)
        case $CBLOCK_FS in
        zfs)
            umount_reverse_order
            build_root_vol=$(path_to_vol "${data_root}/instances/${instance}")
            snap=$(zfs_lookup_origin "${build_root_vol}")
            zfs destroy -R "${build_root_vol}"
            zfs destroy "${snap}"
            ;;
        ufs)
            umount_reverse_order
            ;;
        esac
        ;;
    esac
}

kill_jail
cleanup
network_cleanup
