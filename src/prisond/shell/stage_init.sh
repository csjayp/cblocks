#!/bin/sh
#
set -e

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
    devfs -m "${build_root}/${stage_index}/root/dev" rule applyset
}

prepare_file_system()
{
    base_root="${data_dir}/images/${base_container}"
    if [ ! -d "${base_root}" ]; then
        #
        # Check to see if we have an ephemeral build stage
        if [ ! -h "${build_root}/images/${base_container}" ]; then
            if [ ! -f "${data_dir}/images/${base_container}.tar.zst" ]; then
                exit 1
            fi
            case $CBLOCK_FS in
            fuse-unionfs|ufs)
                mkdir "${data_dir}/images/${base_container}"
                ;;
            zfs)
                zfs_img_vol=`path_to_vol "${data_dir}/images/${base_container}"`
                zfs create "${zfs_img_vol}"
                ;;
            esac
            printf "\033[1m--\033[0m %s\n" \
              "Image present but not unpacked. Extracting..."
            tar -C "${data_dir}/images/${base_container}" -zxf \
                "${data_dir}/images/${base_container}.tar.zst"
        else
            printf "\033[1m--\033[0m %s\n" "Ephemeral image found from previous stage"
            case $CBLOCK_FS in
            ufs|fuse-unionfs)
                base_root="${build_root}/images/${base_container}"
                ;;
            zfs)
                base_root=`readlink "${build_root}/images/${base_container}"`
                ;;
            esac
        fi
    fi
    #
    # Make sure we use -o noatime otherwise read operations will result in
    # shadow objects being created which can impact performance.
    #
    case $CBLOCK_FS in
    fuse-unionfs)
        # NB: we should check for the fuse kld here, or load it inthe daemon
        union_dir="${data_dir}/unions/${instance_name}/${stage_index}"
        mkdir -p "${union_dir}"
        perms="RO"
        if [ -h ${base_root} ] && [ ${stage_index} -gt 0 ]; then
            # NB: we should also validate that this directory matches the
            # structure of an intermediate build image (pattern)
            #
            # NB: Do we need to unmount the active union before mounting
            # it as the base image for the subsquent stage?
            perms="RW"
        fi
        unionfs \
          -o noauto_cache \
          -o sync_read \
          -o big_writes \
          -o intr \
          -o default_permissions \
          -o allow_other \
          -o cow \
          -o use_ino \
          "${base_root}/root=${perms}:${union_dir}=RW" \
          ${build_root}/${stage_index}/root
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
        while `true`; do
            df "${build_root}/${stage_index}/root/etc"
            if [ $? -eq 0 ]; then
                break
            fi
        done
        if [ ! -d "${build_root}/${stage_index}/root/tmp" ] ; then
            mkdir "${build_root}/${stage_index}/root/tmp"
        fi
        ;;
    ufs)
        mount -t unionfs -o noatime -o below ${base_root}/root \
          ${build_root}/${stage_index}/root
        if [ ! -d "${build_root}/${stage_index}/root/tmp" ] ; then
            mkdir "${build_root}/${stage_index}/root/tmp"
        fi
        ;;
    zfs)
        build_root_vol=`path_to_vol "${build_root}"`
        base_root_vol=`path_to_vol "${base_root}"`
        zfs snapshot "${base_root_vol}@${instance_name}"
        zfs clone "${base_root_vol}@${instance_name}" \
          ${build_root_vol}/${stage_index}
        ;;
    esac
}

path_to_vol()
{
    echo -n "$1" | sed -E "s,^/(.*),\1,g"
}

bootstrap()
{
    case $CBLOCK_FS in
    ufs|fuse-unionfs)
        mkdir -p "${build_root}/${stage_index}"
        ;;
    zfs)
        build_root_vol=`path_to_vol "${build_root}"`
        if [ "${stage_index}" == "0" ]; then
            zfs create "${build_root_vol}"
        fi
        ;;
    esac

    prepare_file_system

    if [ ! -d "${build_root}/${stage_index}/root/tmp" ]; then
        mkdir "${build_root}/${stage_index}/root/tmp"
    fi
    stage_work_dir=`mktemp -d "${build_root}/${stage_index}/root/tmp/XXXXXXXX"`
    tar -C "${stage_work_dir}" -zxf "${build_context}"
    chmod +x "${build_root}.${stage_index}.sh"
    cp -p "${build_root}.${stage_index}.sh" "${build_root}/${stage_index}/root/prison-bootstrap.sh"

    VARS="${build_root}/${stage_index}/root/prison_build_variables.sh"
    stage_tmp_dir=`echo ${stage_work_dir} | sed s,${build_root}/${stage_index}/root,,g`
    printf "stage_tmp_dir=${stage_tmp_dir}\nstage_tmp_dir=${stage_tmp_dir}\n \
      \nbuild_root=${build_root} \
      \nstage_index=${stage_index}\nstages=${stage_deps_mount}\n" > $VARS
    bind_devfs

    if [ "${stage_name}" ]; then
        if [ ! -d "${build_root}/images" ]; then
            mkdir "${build_root}/images"
        fi
        ln -s "${build_root}/${stage_index}" "${build_root}/images/${stage_name}" 
    fi
}

bootstrap
