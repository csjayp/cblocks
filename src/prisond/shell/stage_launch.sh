#!/bin/sh
#
set -e 
set -x

data_root="$1"
image_name="$2"
instance_id="$3"
entry_point_args="$4"

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
        if [ -f "${data_root}/images/${image_name}.tar.gz" ]; then
            echo "Extracting image"
            mkdir "${data_root}/images/${image_name}"
            tar -C "${data_root}/images/${image_name}" -zxf \
              "${data_root}/images/${image_name}.tar.gz"
        else
            echo "[FATAL]: no such image ${image_name} downloaded"
            exit 1
        fi
    fi
    instance_root="${data_root}/instances/${instance_id}"
    mkdir -p "${instance_root}"
    mount -t unionfs -o noatime -o below \
      "${data_root}/images/${image_name}/root" \
      "${instance_root}" 
    mount -t devfs devfs "${instance_root}/dev"
    ip4=`get_default_ip`
    instance_cmd=`emit_entrypoint`
    jail -c \
      "host.hostname=${instance_id}" \
      "ip4.addr=${ip4}" \
      "name=${instance_id}" \
      "osrelease=12.1-RELEASE" \
      "path=${instance_root}" \
      command=${instance_cmd}
}

do_launch
