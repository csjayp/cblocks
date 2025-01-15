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
    if [ ! -f "$data_root"/networks/cur ]; then
        return 0
    fi
    newdb=$(mktemp)
    while read ln; do
        ins=$(echo "$ln" | awk -F: '{ print $2 }')
        if [ "$ins" != "$instance" ]; then
            echo $ln >> $newdb
            continue
        fi
        type=$(echo "$ln" | awk -F: '{ print $1 }')
        case $type in
        bridge)
            epair=$(echo $ln | awk -F: '{ print $3 }')
            bridgeif=$(echo $ln | awk -F: '{ print $4 }')
            ifconfig "${epair}a" down
            ifconfig "$bridgeif" deletem "${epair}a"
            ifconfig "${epair}a" destroy
            ;;
        nat)
            version=$(echo "$ln" | cut -f 5 -d,)
            ip=$(echo "$ln" | awk -F: '{ print $3 }')
            if [ "$version" = "6" ]; then
                ifconfig cblock0 inet6 "${ip}" delete
            else
                pfctl -a cblock-nat/"${instance}" -Fa
                pfctl -a cblock-rdr/"${instance}" -Fa
                ifconfig cblock0 "${ip}"/32 delete
            fi
            ;;
        esac
    done < "$data_root"/networks/cur
    rm "$data_root"/networks/cur
    mv $newdb "$data_root"/networks/cur
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
    # For builds the jail should have exited already
    jail_id=$(jls | grep "$instance" | awk '{ print $1 }')
    if [ "$jail_id" ]; then
        jail -r $jail_id
        if [ $? -ne 0 ]; then
            echo "jail instance not present? continuing with fs cleanup"
        fi
    fi
    case $type in
    build)
        rm "${data_root}/instances/${instance}.tar.gz"
        rm ${data_root}/instances/${instance}.*.sh
        rm -fr "${data_root}/instances/${instance}/images"
        stage_list=$(echo "${data_root}"/instances/"${instance}"/[0-9]*)
        for d in $stage_list; do
            umount -f "${d}/root/dev"
            case $CBLOCK_FS in
            ufs)
                umount -f "${d}/root"
                ;;
            esac
        done
        case $CBLOCK_FS in
        zfs)
            build_root_vol=$(path_to_vol "${data_root}/instances/${instance}")
            zfs destroy -f -r "${build_root_vol}"
            ;;
        esac
        ;;
    regular)
        case $CBLOCK_FS in
        fuse-unionfs)
            umount_reverse_order
            chflags -R noschg "${data_root}/unions/${instance}"
            rm -fr "${data_root}/unions/${instance}"
            ;;
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

cleanup
network_cleanup
