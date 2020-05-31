#!/bin/sh
#
data_root="$1"
instance="$2"
type=$3

path_to_vol()
{
    echo -n "$1" | sed -E "s,^/(.*),\1,g"
}

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
    build)
        rm -fr "${data_root}/instances/${instance}/images"
        stage_list=`echo ${data_root}/instances/${instance}/[0-9]*`
        echo "Removing stages"
        for d in $stage_list; do
            umount "${d}/root/dev"
            case $CBLOCK_FS in
            ufs)
                umount -f "${d}/root"
                ;;
            esac
        done
        case $CBLOCK_FS in
        zfs)
            build_root_vol=`path_to_vol "${data_root}/instances/${instance}"`
            # Recursively remove the ZFS datasets (snapshots and file systems)
            zfs destroy -r "${build_root_vol}"
            ;;
        esac
        ;;
    regular)
        umount_reverse_order
        ;;
    esac
}

cleanup
kill_jail
