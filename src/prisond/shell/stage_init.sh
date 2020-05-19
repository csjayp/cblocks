#!/bin/sh
#
set -e 
set -x 

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

get_image()
{
    image_extensions="txz tgz tar.xz tar.gz"
    for ext in $image_extensions; do
        if [ -f "${data_dir}/images/${base_container}.${ext}" ]; then
            echo "${data_dir}/images/${base_container}.${ext}"
            return
        fi
    done
    echo ""
}

ufs_do_setup()
{
    base_root="${data_dir}/images/${base_container}"
    echo "Checking for the presence of base image ${base_container}"
    if [ ! -d "${base_root}" ]; then
        #
        # Check to see if we have an ephemeral build stage
        if [ ! -h "${build_root}/images/${base_container}" ]; then
            echo "Not instantiaed, looking for image file ${base_container}"
            image=`get_image`
            if [ ! $image ]; then
                    echo "Image ${base_container} has not been downloaded"
                    exit 1
            else
                echo "Extracting base into into ${data_dir}/images/${base_container}..."
                mkdir "${data_dir}/images/${base_container}"
                tar -C "${data_dir}/images/${base_container}" -zxf $image
            fi
        else
            echo "Ephemeral image found from previous stage"
            base_root="${build_root}/images/${base_container}"
        fi
    fi
    echo "Image located and ready for use"
    echo "Underlaying image ${base_container}"

    #
    # Make sure we use -o noatime otherwise read operations will result in
    # shadow objects being created which can impact performance.
    #
    mount -t unionfs -o noatime -o below ${base_root}/root ${build_root}/${stage_index}/root
    if [ ! -d "${build_root}/${stage_index}/root/tmp" ] ; then
        mkdir "${build_root}/${stage_index}/root/tmp"
    fi
}

bootstrap()
{

    if [ ! -d "${build_root}/${stage_index}" ]; then
        echo "Prison daemon didn't create the stage root?"
        exit 1
    fi

    echo calling ufs_do_setup
    ufs_do_setup

    stage_deps_dir=`mktemp -d "${build_root}/${stage_index}/root/tmp/deps.XXXXXXX"`
    echo "Creating initial work directory in stage ${stage_index} environment"
    stage_work_dir=`mktemp -d "${build_root}/${stage_index}/root/tmp/XXXXXXXX"`
    echo "Extracting build context..."
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
