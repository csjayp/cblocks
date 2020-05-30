#!/bin/sh
#
set -x

data_root="$1"
instance="$2"
type=$3

kill_jail()
{
    pattern=`echo $instance | awk '{ printf("%.10s", $1) }'`
    jail_id=`jls | grep -F "$pattern" | awk '{ print $1 }'`
    if [ "$jail_id" ]; then
        jail -r $jail_id
    fi
}

umount_reverse_order()
{
    for fs in `mount -p | awk '{ print $2 }' | grep -F "${instance}" | tail -r`; do
        umount -f "$fs"
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
kill_jail
