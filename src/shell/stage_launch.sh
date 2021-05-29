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
image_name="$2"
instance_id="$3"
mount_spec="$4"
network="$5"
tag="$6"
ports="$7"
entry_point_args="$8"
devfs_mount="${data_root}/instances/${instance_id}/root/dev"
image_dir=""

net_is_ip6()
{   
    while read ln; do
        n_name=$(echo "$ln" | awk -F, '{ print $2 }')
        n_version=$(echo "$ln" | awk -F, '{ print $5 }')
        if [ $n_name = "$1" ]; then
            echo $n_version
            return
        fi
    done < ${data_root}/networks/network_list
    echo 4
}

network_is_bridge()
{
    for netif in $(ifconfig -l); do
        if [ "$netif" = "$network" ]; then
            b=$(ifconfig "$network" | grep groups | awk '{ print $2 }')
            if [ "$b" = "bridge" ]; then
                echo TRUE
                return
            fi
        fi
    done
    echo FALSE
}

get_jail_interface()
{
    bridge=$(network_is_bridge)
    case $bridge in
    TRUE)
        epair=$(ifconfig epair create)
        if [ $? -ne 0 ]; then
            echo "Failed to create epair interface"
            exit 1
        fi
        epair_unit=$(echo $epair | sed -E "s/epair([0-9+])a/\1/g")
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
        echo "bridge:${instance_id}:epair${epair_unit}:$network" >> \
          $data_root/networks/cur
        ;;
    *)
        echo "Only bridges are supported at this time"
        exit 1
    esac
}

setup_port_redirects()
{
    _all_fields=$1
    _ip=$2
    _outif=$3
    for spec in $(echo "${_all_fields}" | sed "s/,/ /g"); do
        case $spec in
        none)
            return
            ;;
        *:*:*)
            for field in $(jot 4); do
                case $field in
                1)
                    host_port=$(echo "$spec" | cut -f $field -d:)
                    ;;
                2)
                    container_port=$(echo "$spec" | cut -f $field -d:)
                    ;;
                3)
                    visibility=$(echo "$spec" | cut -f $field -d:)
                    ;;
                esac
            done
            echo "rdr on $_outif inet proto tcp from any to any " \
              "port $host_port -> $_ip port $container_port"
            ;;
        *:*)
            for field in $(jot 4); do
                case $field in
                1)
                    host_port=$(echo "$spec" | cut -f $field -d:)
                    ;;
                2)
                    container_port=$(echo "$spec" | cut -f $field -d:)
                    ;;
                esac
            done
            echo "rdr on lo0 inet proto tcp from any to any " \
              "port $host_port -> $_ip port $container_port"
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
        # NB: NOTYET
        # Expose /dev/zfs for snapshotting et al
        # devfs -m ${devfs_mount} ruleset 4
        # devfs -m ${devfs_mount} rule applyset
        ;;
    esac
    bridge=$(network_is_bridge)
    if [ "$bridge" = "TRUE" ]; then
        bpf_allowed=$(devfs rule -s 5000 show | grep -c "bpf\* unhide")
        if [ "$bpf_allowed" -eq 0 ]; then
            devfs -m ${devfs_mount} ruleset 5000
            devfs rule -s 5000 add path 'bpf*' unhide
        fi
        devfs -m ${devfs_mount} ruleset 5000
        devfs -m ${devfs_mount} rule applyset
    fi
}

emit_os_release()
{
    if [ -f "${image_dir}/OSRELEASE" ]; then
        cat "${image_dir}/OSRELEASE"
    else
        uname -r
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
    case $3 in
    4)
        family="inet"
        ;;
    6)
        family="inet6"
        ;;
    *)
        echo "invalid ip version"
        exit 1
    esac
    range=`subcalc $family $1 | grep "^range:"`
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
    case $2 in
    4)
        family="inet"
        ;;
    6)
        family="inet6"
        ;;
    *)
        echo "invalid ip version"
        exit 1
    esac
    for ip in $(ifconfig cblock0 | grep "$family" | awk '{ print $2 }'); do
        if [ "$ip" = "$1" ]; then
            echo yes
            return 0
        fi
    done
    echo no
}

network_is_defined()
{
    if [ "$network" = "__host__" ]; then
        return 0
    fi
    while read ln; do
        n_name=$(echo $ln | awk -F, '{ print $2 }')
        if [ "$n_name" = "$network" ]; then
            return $(true)
        fi
    done < $data_root/networks/network_list
    echo "Network $network is not defined"
    exit 1
}

network_to_ip6()
{
    while read ln; do
        n_type=$(echo $ln | awk -F, '{ print $1 }')
        if [ "$n_type" != "nat" ]; then
            continue
        fi
        n_name=$(echo $ln | awk -F, '{ print $2 }')
        if [ "$n_name" != "$network" ]; then
            continue
        fi
        net_addr=$(echo $ln | awk -F, '{ print $4 }')
        out_if=$(echo $ln | awk -F, '{ print $3 }')
        version=$(echo $ln | awk -F, '{ print $5 }')
        if [ $version != "6" ]; then
            continue
        fi
        for ip in $(subcalc inet6 $net_addr print | grep -v "^;"); do
            if [ $(is_assigned $ip $version) = "no" ] && \
               [ $(is_broadcast $net_addr $ip $version) = "no" ]; then
                ifconfig cblock0 inet6 "${ip}/128" alias
                echo "${ip}"
                echo "nat,${instance_id},${ip},$network,6" >> \
                  $data_root/networks/cur
                return
            fi
        done
    done < $data_root/networks/network_list
    exit 1
}

network_to_ip()
{
    while read ln; do
        n_type=$(echo $ln | awk -F, '{ print $1 }')
        if [ "$n_type" != "nat" ]; then
            continue
        fi
        n_name=$(echo $ln | awk -F, '{ print $2 }')
        if [ "$n_name" != "$network" ]; then
            continue
        fi
        net_addr=$(echo $ln | awk -F, '{ print $4 }')
        out_if=$(echo $ln | awk -F, '{ print $3 }')
        for ip in $(subcalc inet $net_addr print | grep -v "^;"); do
            if [ $(is_assigned $ip) = "no" ] && \
               [ $(is_broadcast $net_addr $ip) = "no" ]; then
                ifconfig cblock0 inet "${ip}/32" alias
                echo "nat on $out_if from ${ip}/32 to any -> ($out_if)" | \
                    pfctl -a cblock-nat/${instance_id} -f -
                echo "nat,${instance_id},${ip},$network,4" >> \
                  $data_root/networks/cur
                echo "${ip}"
                setup_port_redirects "$ports" "$ip" "$out_if" | \
                  pfctl -a cblock-rdr/${instance_id} -f -
                return
            fi
        done
    done < $data_root/networks/network_list
    exit 1
}

do_launch()
{
    if [ $(sysctl net.inet.ip.forwarding | awk '{ print $2 }') != "1" ]; then
        sysctl net.inet.ip.forwarding=1 2>&1 >/dev/null
        if [ $? -ne 0 ]; then
            echo "failed to enable forwarding"
            exit 1
        fi
    fi
    img_tag="${image_name}:${tag}"
    if [ ! -h "${data_root}/images/${img_tag}" ]; then
        echo "[FATAL]: no such image ${image_name} downloaded"
        exit 1
    fi
    image_dir=`readlink "${data_root}/images/${img_tag}"`
    instance_hostname=`printf "%10.10s" ${instance_id}`
    instance_root="${data_root}/instances/${instance_id}/root"
    case $CBLOCK_FS in
    fuse-unionfs)
        mkdir -p "${instance_root}"
        union_dir="${data_dir}/unions/${instance_name}/${stage_index}"
        mkdir -p "${union_dir}"
        perms="RO"
        unionfs \
          -o big_writes \
          -o intr \
          -o default_permissions \
          -o allow_other \
          -o cow \
          -o use_ino \
          "${image_dir}/root:${union_dir}=RW" \
          "${instance_root}"
        if [ $? -ne 0 ]; then
            echo "Failed to add the fuse/unionfs overlay"
            exit 1
        fi
        # This is a bit hackisk, but we need to wait for the unionfs mount to
        # register in the mount points. This operation is occuring async to
        # this shell script. This might be better handled by the unionfs
        # command line tool itself
        #
        # NB: add an upper bound to this loop, either time or count.
        #while $(true); do
        #    df "${instance_root}/etc"
        #    if [ $? -eq 0 ]; then
        #        break
        #    fi
        #done
        ;;
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
    is_bridge=$(network_is_bridge)
    set $(emit_entrypoint)
    if [ "$is_bridge" = "TRUE" ]; then
       netif=$(get_jail_interface)
       jail -c \
          "host.hostname=${instance_hostname}" \
          "vnet" \
          "vnet.interface=$netif" \
          "name=${image_name}-${instance_hostname}" \
          "osrelease=$(emit_os_release)" \
          "path=${instance_root}" \
          command="$@"
    else
        if [ "$network" = "__host__" ]; then
            ip4=$(get_default_ip)
        fi
        jailcmd="jail -c host.hostname=${instance_hostname} "
        jailcmd="$jailcmd name=${image_name}-${instance_hostname} "
        jailcmd="$jailcmd allow.chflags=1 path=${instance_root} "
        if [ $(net_is_ip6 $network) = "6" ]; then
            netspec="ip6.addr=$(network_to_ip6)"
        else
            netspec="ip4.addr=$ip4"
        fi
        jailcmd="$jailcmd $netspec command=$@"
        eval $jailcmd
    fi
}

launch_block()
{
    network_is_defined
    do_launch
}

launch_block
