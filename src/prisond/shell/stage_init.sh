#!/bin/sh
#
set -e 

build_root=$1
stage_index=$2
base_container=$3
data_dir=$4
build_context=$5

cleanup()
{
    umount "${build_root}/${stage_index}"
    chflags -R noschg "${build_root}/${stage_index}"
    rm -fr "${build_root}/${stage_index}"
}

bootstrap()
{
    if [ -d "${build_root}/${stage_index}" ]; then
        echo "staging directory already exists?!"
        exit 1
    fi
    echo "Checking for the presence of base image ${base_container}"
    if [ ! -d "${data_dir}/images/${base_container}" ]; then
        echo "Not instantiaed, looking for image file ${base_container}"
        if [ ! -f "${data_dir}/images/${base_container}.txz" ]; then
                echo "Image ${base_container} has not been downloaded"
                exit 1
        else
            echo "Extracting base into into ${data_dir}/images/${base_container}..."
            mkdir "${data_dir}/images/${base_container}"
            tar -C "${data_dir}/images/${base_container}" -zxf \
                "${data_dir}/images/${base_container}.txz"
        fi
    else
        echo "Image located and ready for use"
    fi
    echo "Creating stage ${stage_index} environment for ${base_container}"
    mkdir "${build_root}/${stage_index}"

    echo "Underlaying image ${base_container}"
    mount -t unionfs -o noatime -o below "${data_dir}/images/${base_container}" \
        "${build_root}/${stage_index}"

    echo "Creating initial work directory in stage ${stage_index} environment"
    stage_work_dir=`mktemp -d "${build_root}/${stage_index}/tmp/XXXXXXXX"`
    echo "Extracting build context..."
    tar -C "${stage_work_dir}" -zxf "${build_context}"
}

bootstrap
cleanup

