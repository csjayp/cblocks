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
build_root=$1
stage_index=$2
base_container=$3
data_dir=$4
build_context=$5
stage_deps=$6
instance_name=$7
stage_name=""
if [ "$8" ]; then
    stage_name=$8
fi

stage_deps_dir=""

bind_devfs()
{
    if [ ! -d "${build_root}/${stage_index}/dev" ]; then
        mkdir "${build_root}/${stage_index}/dev"
    fi
    mount -t devfs devfs "${build_root}/${stage_index}/root/dev"
    devfs -m "${build_root}/${stage_index}/root/dev" ruleset 5000
    if [ $(devfs rule -s 5000 show | grep -c .) -eq 0 ]; then
        devfs rule -s 5000 add hide
        devfs rule -s 5000 add path null unhide
        devfs rule -s 5000 add path zero unhide
        devfs rule -s 5000 add path random unhide
        devfs rule -s 5000 add path urandom unhide
        devfs rule -s 5000 add path 'fd'  unhide
        devfs rule -s 5000 add path 'fd/*'  unhide
        devfs rule -s 5000 add path stdin unhide
        devfs rule -s 5000 add path stdout unhide
        devfs rule -s 5000 add path stderr unhide
    fi
    devfs -m "${build_root}/${stage_index}/root/dev" rule applyset
}

prepare_file_system()
{
    # Check to see if the tag has been specified. If not, then prepend latest

    dc=$(echo "${base_container}" | grep -Fc :)
    if [ "$dc" -ne 0 ]; then
        _base="${data_dir}/images/${base_container}"
    else
        _base="${data_dir}/images/${base_container}:latest"
    fi
    if [ -h "$_base" ]; then
        readlnk=$(readlink "$_base")
        base_root=$(realpath "${readlnk}")
    else
        # Check to see if we have an ephemeral build stage
        if [ -h "${build_root}/images/${base_container}" ]; then
            printf "\033[1m--\033[0m %s\n" "Ephemeral image found from previous stage"
            case $CBLOCK_FS in
            ufs)
                base_root="${build_root}/images/${base_container}"
                ;;
            zfs)
                base_root=$(readlink "${build_root}/images/${base_container}")
                ;;
            esac
        else
            echo "Error: no such image"
            exit 1
        fi
    fi
    #
    # Make sure we use -o noatime otherwise read operations will result in
    # shadow objects being created which can impact performance.
    #
    case $CBLOCK_FS in
    ufs)
        mount -t unionfs -o noatime -o below "${base_root}"/root \
          "${build_root}"/"${stage_index}"/root
        if [ ! -d "${build_root}/${stage_index}/root/tmp" ] ; then
            mkdir "${build_root}/${stage_index}/root/tmp"
        fi
        ;;
    zfs)
        build_root_vol=$(path_to_vol "${build_root}")
        base_root_vol=$(path_to_vol "${base_root}")
        zfs snapshot "${base_root_vol}@${instance_name}_${stage_index}"
        zfs clone "${base_root_vol}@${instance_name}_${stage_index}" \
          "${build_root_vol}/${stage_index}"
        ;;
    esac
}

path_to_vol()
{
    echo -n "$1" | sed -E "s,^/(.*),\1,g"
}

get_dep_list()
{
    find "${data_dir}/instances" \
      -name "copy_from_${instance_name}_*" -type f \
      -maxdepth 1
}

extract_previous_stage_deps()
{
    for f in $(get_dep_list); do
        unit=$(echo "$f" | sed -E 's/.*_([[:xdigit:]]+)_([0-9]+).tar/\2/g')
        targ="${build_root}/${stage_index}/root/tmp/stage${unit}"
        mkdir "${targ}"
        tar -C "${targ}" -xpf "$f"
        rm "$f"
    done
}

bootstrap()
{

    case $CBLOCK_FS in
    ufs)
        mkdir -p "${build_root}/${stage_index}"
        ;;
    zfs)
        if ! [ -d "$data_dir/instances" ]; then
            zfs create $(path_to_vol $data_dir/instances)
        fi
        build_root_vol=$(path_to_vol "${build_root}")
        if [ "${stage_index}" == "0" ]; then
            zfs create "${build_root_vol}"
        fi
        ;;
    esac
    prepare_file_system
    if [ ! -d "${build_root}/${stage_index}/root/tmp" ]; then
        mkdir "${build_root}/${stage_index}/root/tmp"
    fi
    extract_previous_stage_deps
    stage_work_dir=$(mktemp -d "${build_root}/${stage_index}/root/tmp/XXXXXXXX")
    tar -C "${stage_work_dir}" -zxf "${build_context}"

    chmod +x "${build_root}.${stage_index}.sh"
    cp -p "${build_root}.${stage_index}.sh" "${build_root}/${stage_index}/root/tmp/cblock-bootstrap.sh"
    VARS="${build_root}/${stage_index}/root/tmp/cblock_build_variables.sh"
    stage_tmp_dir=$(echo "${stage_work_dir}" | sed s,"${build_root}"/"${stage_index}"/root,,g)
    printf "stage_tmp_dir=${stage_tmp_dir}\nstage_tmp_dir=${stage_tmp_dir}\n \
      \nbuild_root=${build_root} \
      \nstage_index=${stage_index}\nstages=${stage_deps_mount}\n" > "$VARS"
    bind_devfs

    if [ "${stage_name}" ]; then
        if [ ! -d "${build_root}/images" ]; then
            mkdir "${build_root}/images"
        fi
        ln -s "${build_root}/${stage_index}" "${build_root}/images/${stage_name}" 
    fi
}

bootstrap
