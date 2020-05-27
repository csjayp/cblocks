#!/bin/sh
#
set -x

data_root="$1"
instance="$2"
type=$3

umount_reverse_order()
{
    for fs in `mount -p | awk '{ print $2 }' | grep -F "${instance}" | tail -r`; do
        umount $fs
    done
}

cleanup()
{
    case "$type" in
    1)
        rm -fr "${data_root}/instances/${instance}/images"
        stage_list=`echo ${data_root}/instances/${instance}/[0-9]*`
        for d in $stage_list; do
            umount "${d}/root/dev"
            #
            # NB: we need to inevestigate why this is required after
            # multistage builds (forced umount)
            #
            umount -f "${d}/root"
        done
        ;;
    2)
        umount_reverse_order
        ;;
    esac
}

cleanup
