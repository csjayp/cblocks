#!/bin/sh
#
data_root="$1"
image_name="$2"
instance_id="$3"
mount_spec="$4"
network="$5"
tag="$6"
ports="$7"
entry_point_args="$8"
devfs_mount="${data_root}/instances/${instance_id}/root/dev"
image_dir=""

network_is_bridge()
{
    for netif in `ifconfig -l`; do
        if [ "$netif" = "$network" ]; then
            b=`ifconfig "$network" | grep groups | awk '{ print $2 }'`
            if [ "$b" = "bridge" ]; then
                echo TRUE
            fi
        fi
    done
    echo FALSE
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

setup_port_redirects()
{
    _all_fields=$1
    for spec in `echo "${_all_fields}" | sed "s/,/ /g"`; do
        case $spec in
        none)
            return
            ;;
        *:*:*)
            for field in `jot 4`; do
                case $field in
                1)
                    host_port=`echo "$spec" | cut -f $field -d:`
                    ;;
                2)
                    container_port=`echo "$spec" | cut -f $field -d:`
                    ;;
                3)
                    visibility=`echo "$spec" | cut -f $field -d:`
                    ;;
                esac
            done
            ;;
        *:*)
            for field in `jot 4`; do
                case $field in
                1)
                    host_port=`echo "$spec" | cut -f $field -d:`
                    ;;
                2)
                    container_port=`echo "$spec" | cut -f $field -d:`
                    ;;
                esac
            done
            echo "rdr on lo0 inet proto tcp " \
              "from any to any port $host_port -> $2 port $container_port" | \
            pfctl -a cblock-rdr/${instance_id} -f -
            ;;
        esac
    done
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
    devfs -m ${devfs_mount} ruleset 3
    devfs -m ${devfs_mount} rule applyset
    case $CBLOCK_FS in
    zfs)
        # Expose /dev/zfs for snapshotting et al
        devfs -m ${devfs_mount} ruleset 4
        devfs -m ${devfs_mount} rule applyset
        ;;
    esac
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
    CMD=`cat "${image_dir}/ENTRYPOINT"`
    if [ -f "${image_dir}/ARGS" ]; then
        ARGS=`cat "${image_dir}/ARGS"`
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

path_to_vol()
{
    echo -n "$1" | sed -E "s,^/(.*),\1,g"
}

is_broadcast()
{
    range=`subcalc inet $1 | grep "^range:"`
    start=`echo $range | awk '{ print $2 }'`
    end=`echo $range | awk '{ print $4 }'`
    if [ "$2" = "$start" ] || [ "$2" = "$end" ]; then
        echo yes
    else
        echo no
    fi
}

is_assigned()
{
    for ip in `ifconfig cblock0 | grep "inet "| awk '{ print $2 }'`; do
        if [ "$ip" = "$1" ]; then
            echo yes
            return
        fi
    done
    echo no
}

network_to_ip()
{
    while read ln; do
        n_type=`echo $ln | awk -F: '{ print $1 }'`
        if [ "$n_type" != "nat" ]; then
            continue
        fi
        n_name=`echo $ln | awk -F: '{ print $2 }'`
        if [ "$n_name" != "$network" ]; then
            continue
        fi
        net_addr=`echo $ln | awk -F: '{ print $4 }'`
        out_if=`echo $ln | awk -F: '{ print $3 }'`
        for ip in `subcalc inet $net_addr print | grep -v "^;"`; do
            if [ `is_assigned $ip` = "no" ] && \
               [ `is_broadcast $net_addr $ip` = "no" ]; then
                ifconfig cblock0 inet "${ip}/32" alias
                echo "nat on $out_if from ${ip}/32 to any -> ($out_if)" | \
                    pfctl -a cblock-nat/${instance_id} -f -
                echo "nat:${instance_id}:${ip}" >> $data_root/networks/cur
                echo "${ip}"
                return
            fi
        done
    done < $data_root/networks/network_list
    exit 1
}

do_launch()
{
    sysctl net.inet.ip.forwarding=1
    img_tag="${image_name}:${tag}"
    if [ ! -h "${data_root}/images/${img_tag}" ]; then
        echo "[FATAL]: no such image ${image_name} downloaded"
        exit 1
    fi
    image_dir=`readlink "${data_root}/images/${img_tag}"`
    instance_hostname=`printf "%10.10s" ${instance_id}`
    instance_root="${data_root}/instances/${instance_id}/root"
    case $CBLOCK_FS in
    zfs)
        volname=`path_to_vol "${image_dir}"`
        dest_volname=`path_to_vol "${data_root}/instances/${instance_id}"`
        zfs snapshot "$volname@${instance_id}"
        zfs clone "$volname@${instance_hostname}" "${dest_volname}"
        ;;
    ufs)
        mkdir -p "${instance_root}"
        mount -t unionfs -o noatime -o below \
          "${image_dir}/root" "${instance_root}"
        ;;
    esac
    mount -t devfs devfs "${instance_root}/dev"
    config_devfs
    eval `emit_mount_specification "$mount_spec"`
    instance_cmd=$(emit_entrypoint)
    is_bridge=$(network_is_bridge)
    if [ "$is_bridge" = "TRUE" ]; then
       netif=$(get_jail_interface)
       jail -c \
          "host.hostname=${instance_hostname}" \
          "vnet" \
          "vnet.interface=$netif" \
          "name=${instance_id}" \
          "osrelease=12.1-RELEASE" \
          "path=${instance_root}" \
          command="${instance_cmd}"
    else
        ip4=$(network_to_ip)
        setup_port_redirects "$ports" "$ip4"
        #ip4=`get_default_ip`
        jail -c \
          "host.hostname=${instance_hostname}" \
          "ip4.addr=${ip4}" \
          "name=${instance_id}" \
          "osrelease=12.1-RELEASE" \
          "path=${instance_root}" \
          command="${instance_cmd}"
    fi
}

do_launch
