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
. "$(dirname "$0")/common.sh"
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

#set -e

# The network is an interface configured by the administrator: a bridge
# (VNET instance attached via epair), a loopback carrying the NAT subnet as its
# base address (non-VNET instance with an address alias), or __host__.
network_type()
{
    if [ "$network" = "__host__" ]; then
        echo host
        return
    fi
    if [ "$network" = "lo0" ] || ! ifconfig "$network" >/dev/null 2>&1; then
        echo none
        return
    fi
    case $(ifconfig -D "$network" | awk '/groups:/ { print $2 }') in
    bridge)
        echo bridge
        ;;
    lo)
        echo nat
        ;;
    *)
        echo none
        ;;
    esac
}

# Print the NAT subnet the administrator assigned to the loopback, e.g.
# 10.0.0.1/24. Instance aliases are host addresses (/32 or /128) so they are
# skipped, as are IPv6 link-local addresses.
nat_base_cidr()
{
    ifconfig -f inet:cidr,inet6:cidr "$network" $1 | \
      awk -v fam=$1 '$1 == fam && $2 !~ /^fe80:/ && $2 !~ /\/(32|128)$/ \
      { print $2; exit }'
}

nat_ip_version()
{
    if [ "$(nat_base_cidr inet)" ]; then
        echo 4
    elif [ "$(nat_base_cidr inet6)" ]; then
        echo 6
    fi
}

# Outbound interface for port redirects: the loopback's description if the
# administrator set one, otherwise the interface holding the default route.
nat_outif()
{
    _desc=$(ifconfig "$network" | awk '/description:/ { print $2 }')
    if [ "$_desc" ] && ifconfig "$_desc" >/dev/null 2>&1; then
        echo "$_desc"
        return
    fi
    route -n get default 2>/dev/null | awk '/interface:/ { print $2 }'
}

get_jail_interface()
{
    # NB: big cleanup on errors here needed!
    epair=$(ifconfig epair create)
    if [ $? -ne 0 ]; then
        echo "Failed to create epair interface" >&2
        return 1
    fi
    epair_unit=$(echo $epair | sed -E "s/epair([0-9]+)a/\1/g")
    ifconfig epair${epair_unit}a up && ifconfig epair${epair_unit}b up
    if [ $? -ne 0 ]; then
        echo "Failed to bring epair interfaces up" >&2
        return 1
    fi
    ifconfig $network addm epair${epair_unit}a
    if [ $? -ne 0 ]; then
        echo "Failed to add epair interface to bridge $network" >&2
        return 1
    fi
    # Make sure the underlying bridge interface is UP
    ifconfig $network up
    if [ $? -ne 0 ]; then
        echo "Unable to bring bridge interface $network UP" >&2
        return 1
    fi
    echo epair${epair_unit}b
    echo "bridge,${instance_id},epair${epair_unit},$network" >> \
      $data_root/networks/cur
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

# Print the host path of the mount point for container path $1, creating
# it if it doesn't exist. Fail if it would land outside the instance root,
# for example through "..", or a symlink in the image that points outside.
mount_point()
{
    _root=$(realpath "${data_root}/instances/${instance_id}/root")
    case "/$1/" in
    */../*)
        echo "$1: volume path may not contain .." >&2
        return 1
        ;;
    esac
    # Resolve the part that exists, and keep the rest to create.
    _existing="${_root}/$1"
    _rest=""
    while [ ! -e "$_existing" ]; do
        _rest="/$(basename "$_existing")${_rest}"
        _existing=$(dirname "$_existing")
    done
    _real=$(realpath "$_existing")
    case "${_real}/" in
    "${_root}"/*)
        ;;
    *)
        echo "$1: volume path leaves the instance root" >&2
        return 1
        ;;
    esac
    mkdir -p "${_real}${_rest}" || return 1
    echo "${_real}${_rest}"
}

# Mount the comma separated volume list $1: devfs (mounted elsewhere),
# tmpfs, procfs, fdescfs, or fs_type:host_path:container_path:ro|rw.
# The specs come from the client, so they are only ever passed to mount
# as arguments, never evaluated by the shell.
mount_volumes()
{
    _ifs="$IFS"
    set -f
    IFS=,
    set -- $1
    IFS="$_ifs"
    set +f
    for spec; do
        case "$spec" in
        devfs)
            continue
            ;;
        tmpfs)
            _mnt=$(mount_point /tmp) && mount -t tmpfs tmpfs "$_mnt"
            ;;
        procfs)
            _mnt=$(mount_point /proc) && mount -t procfs procfs "$_mnt"
            ;;
        fdescfs)
            _mnt=$(mount_point /dev/fd) && mount -t fdescfs fdescfs "$_mnt"
            ;;
        *)
            IFS=: read -r fs_type fs_host container perms extra <<EOF
$spec
EOF
            if [ -z "$fs_type" ] || [ -z "$fs_host" ] || \
              [ -z "$container" ] || [ -n "$extra" ]; then
                echo "$spec: must follow fs:local:container:perms" >&2
                return 1
            fi
            case "$perms" in
            ro|RO)
                _opts="-o ro"
                ;;
            rw|RW)
                _opts=""
                ;;
            *)
                echo "$spec: perms must be ro or rw" >&2
                return 1
                ;;
            esac
            _mnt=$(mount_point "$container") && \
              mount -t "$fs_type" $_opts "$fs_host" "$_mnt"
            ;;
        esac
        if [ $? -ne 0 ]; then
            echo "$spec: mount failed" >&2
            return 1
        fi
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
    if [ "$(network_type)" = "bridge" ]; then
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
    range=$(subcalc $family $1 | grep "^range:")
    start=$(echo $range | awk '{ print $2 }')
    end=$(echo $range | awk '{ print $4 }')
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
    for ip in $(ifconfig "$network" $family | awk -v fam=$family \
      '$1 == fam { print $2 }'); do
        if [ "$ip" = "$1" ]; then
            echo yes
            return 0
        fi
    done
    echo no
}

network_is_defined()
{
    case $(network_type) in
    host|bridge)
        return 0
        ;;
    nat)
        ;;
    *)
        echo "Network $network is not a bridge or NAT loopback interface"
        exit 1
        ;;
    esac
    case $(nat_ip_version) in
    4)
        fwd=net.inet.ip.forwarding
        ;;
    6)
        fwd=net.inet6.ip6.forwarding
        ;;
    *)
        echo "NAT interface $network has no network address configured"
        exit 1
        ;;
    esac
    if [ "$(sysctl -n $fwd)" != "1" ]; then
        echo "$fwd must be enabled for NAT networks (see gateway_enable)"
        exit 1
    fi
}

network_to_ip6()
{
    net_addr=$(nat_base_cidr inet6)
    for ip in $(subcalc inet6 $net_addr print | grep -v "^;"); do
        if [ $(is_assigned $ip 6) = "no" ] && \
           [ $(is_broadcast $net_addr $ip 6) = "no" ]; then
            ifconfig "$network" inet6 "${ip}/128" alias
            echo "${ip}"
            echo "nat,${instance_id},${ip},$network,6" >> \
              $data_root/networks/cur
            return
        fi
    done
    exit 1
}

network_to_ip()
{
    net_addr=$(nat_base_cidr inet)
    for ip in $(subcalc inet $net_addr print | grep -v "^;"); do
        if [ $(is_assigned $ip 4) = "no" ] && \
           [ $(is_broadcast $net_addr $ip 4) = "no" ]; then
            ifconfig "$network" inet "${ip}/32" alias
            echo "nat,${instance_id},${ip},$network,4" >> \
              $data_root/networks/cur
            echo "${ip}"
            rdr_rules=$(setup_port_redirects "$ports" "$ip" "$(nat_outif)")
            if [ "$rdr_rules" ]; then
                echo "$rdr_rules" | pfctl -a cblock-rdr/${instance_id} -f -
            fi
            return
        fi
    done
    exit 1
}

do_launch()
{
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
        # See stage_bootstrap_build.sh for why whiteout=whenneeded.
        mount -t unionfs -o noatime -o below -o whiteout=whenneeded \
          "${image_dir}/root" "${instance_root}"
        ;;
    esac
    mount -t devfs devfs "${instance_root}/dev"
    config_devfs
    mount_volumes "$mount_spec" || exit 1
    net_type=$(network_type)
    set $(emit_entrypoint)
    if [ "$net_type" = "bridge" ]; then
       netif=$(get_jail_interface) || exit 1
       jail -c \
          "host.hostname=${instance_hostname}" \
          "vnet" \
          "vnet.interface=$netif" \
          "name=${image_name}-${instance_hostname}" \
          "allow.chflags=1" \
          "osrelease=$(emit_os_release)" \
          "path=${instance_root}" \
          command="$@"
    else
        if [ "$net_type" = "host" ]; then
            netspec="ip4.addr=$(get_default_ip)"
        elif [ $(nat_ip_version) = "6" ]; then
            ip=$(network_to_ip6)
            netspec="ip6.addr=$ip"
        else
            ip=$(network_to_ip)
            netspec="ip4.addr=$ip"
        fi
        if [ "$net_type" = "nat" ] && [ ! "$ip" ]; then
            echo "No free addresses left on NAT interface $network"
            exit 1
        fi
        jailcmd=""
        if [ -f "${image_dir}/AUDITCFG" ]; then
            jailcmd="setaudit -m $(cat ${image_dir}/AUDITCFG) -s $(get_default_ip)"
        fi
        jailcmd="$jailcmd jail -c host.hostname=${instance_hostname} "
        jailcmd="$jailcmd name=${image_name}-${instance_hostname} "
        jailcmd="$jailcmd allow.chflags=1 path=${instance_root} "
        jailcmd="$jailcmd $netspec osrelease=$(emit_os_release) command=$@"
        eval $jailcmd
    fi
}

launch_block()
{
    network_is_defined
    do_launch
}

launch_block
