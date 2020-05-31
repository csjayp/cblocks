#!/bin/sh
#
#set -e 
set -x

data_root="$1"
image_name="$2"
instance_id="$3"
mount_spec="$4"
network="$5"
entry_point_args="$6"
devfs_mount="${data_root}/instances/${instance_id}/root/dev"

network_is_bridge()
{
    b=`ifconfig "$network" | grep groups | awk '{ print $2 }'`
    if [ "$b" = "bridge" ]; then
        echo TRUE
    else
        echo FALSE
    fi
}

get_jail_interface()
{
    bridge=`network_is_bridge`
    case $bridge in
    TRUE)
        epair=`ifconfig epair create`
        if [ $? -ne 0 ]; then
            echo "Failed to create epair interface"
            exit 1
        fi
        epair_unit=`echo $epair | sed -E "s/epair([0-9+])a/\1/g"`
        ifconfig epair${epair_unit}a up && ifconfig epair${epair_unit}b up
        if [ $? -ne 0 ]; then
            echo "Failed to bring epair interfaces up"
            exit 1
        fi
        ifconfig $network addm epair${epair_unit}a
        if [ $? -ne 0 ]; then
            echo "Failed to add epair interface to bridge $network"
            exit 1
        fi
        echo epair${epair_unit}b
        ;;
    *)
        echo "Only bridges are supported at this time"
        exit 1
    esac
}

emit_mount_specification()
{   
    _all_fields=$1
    _root="${data_root}/instances/${instance_id}/root"

    echo "$1" | grep '[;&<>|]'
    if [ $? -eq 0 ]; then
        return 1
    fi
    for spec in `echo "${_all_fields}" | sed "s/,/ /g"`; do
        case $spec in
        tmpfs)
            echo mount -t tmpfs tmpfs ${_root}/tmp\;
            ;;
        devfs)
            ;;
        procfs)
            echo mount -t procfs procfs ${_root}/proc\;
            ;;
        fdescfs)
            echo mount -t fdescfs fdescfs ${_root}/dev/fd\;
            ;;
        *:*:*:*)
            for field in `jot 4`; do
                case $field in
                1)
                    fs_type=`echo "$spec" | cut -f $field -d:`
                    ;;
                2)
                    fs_host=`echo "$spec" | cut -f $field -d:`
                    ;;
                3)
                    container_mount=`echo "$spec" | cut -f $field -d:`
                    ;;
                4)
                    perms=`echo "$spec" | cut -f $field -d:`
                    ;;
                esac
            done
            echo -n "mount -t $fs_type "
            if [ "$perms" = "RO" ] || [ "$perms" = "ro" ]; then
                echo -n "-o ro "
            fi
            echo $fs_host ${_root}/$container_mount\;
            ;;
        *)
            echo invalid specification
            return 1
            ;;
        esac
    done
}

config_devfs()
{
    devfs -m ${devfs_mount} ruleset 1
    devfs -m ${devfs_mount} rule applyset
    devfs -m ${devfs_mount} ruleset 2 
    devfs -m ${devfs_mount} rule applyset
    V=`devfs rule showsets | grep "^5000"`
    if [ ! "$V" ]; then
        devfs -m ${devfs_mount} ruleset 5000
        devfs rule -s 5000 add path 'bpf*' unhide
    fi
    bridge=`network_is_bridge`
    if [ "$bridge" = "TRUE" ]; then
        devfs -m ${devfs_mount} ruleset 5000
        devfs -m ${devfs_mount} rule applyset
    fi
}

emit_entrypoint()
{
    CMD=`cat "${data_root}/images/${image_name}/ENTRYPOINT"`
    if [ -f "${data_root}/images/${image_name}/ARGS" ]; then
        ARGS=`cat "${data_root}/images/${image_name}/ARGS"`
    fi
    if [ "${entry_point_args}" ]; then
        ARGS="${entry_point_args}"
    fi
    if [ "${ARGS}" ]; then
        echo "${CMD} ${ARGS}"
    else
        echo "${CMD}"
    fi
}

get_default_ip()
{
    netif=`route get www.fastly.com | grep -F 'interface:' | awk '{ print $2 }'`
    ipv4=`ifconfig ${netif} | egrep "inet " | tail -n 1 | awk '{ print $2 }'`
    echo "${ipv4}"
}

do_launch()
{
    if [ ! -d "${data_root}/images/${image_name}" ]; then
        if [ -f "${data_root}/images/${image_name}.tar.zst" ]; then
            echo "Extracting image"
            mkdir "${data_root}/images/${image_name}"
            tar -C "${data_root}/images/${image_name}" -zxf \
              "${data_root}/images/${image_name}.tar.zst"
        else
            echo "[FATAL]: no such image ${image_name} downloaded"
            exit 1
        fi
    fi
    instance_root="${data_root}/instances/${instance_id}/root"
    mkdir -p "${instance_root}"
    mount -t unionfs -o noatime -o below \
      "${data_root}/images/${image_name}/root" \
      "${instance_root}" 
    eval `emit_mount_specification $mount_spec`
    mount -t devfs devfs "${instance_root}/dev"
    config_devfs
    ip4=`get_default_ip`
    instance_cmd=`emit_entrypoint`
    instance_hostname=`printf "%10.10s" ${instance_id}`
    echo "booya network $network"
    is_bridge=`network_is_bridge`
    if [ "$is_bridge" = "TRUE" ]; then
       netif=`get_jail_interface`
       jail -c \
          "host.hostname=${instance_hostname}" \
          "vnet" \
          "vnet.interface=$netif" \
          "name=${instance_id}" \
          "osrelease=12.1-RELEASE" \
          "path=${instance_root}" \
          command=${instance_cmd}
    else
        jail -c \
          "host.hostname=${instance_hostname}" \
          "ip4.addr=${ip4}" \
          "name=${instance_id}" \
          "osrelease=12.1-RELEASE" \
          "path=${instance_root}" \
          command=${instance_cmd}
    fi
}

do_launch
