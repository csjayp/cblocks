#!/bin/sh
#
set -e
set -x

build_root=$1
build_index=$2
data_dir=$3
image_name=$4
n_stages=$5

commit_image()
{
    rm -fr "${build_root}/${build_index}/tmp"
    mkdir "${build_root}/${build_index}/tmp"
    tar -C "${build_root}/${build_index}" --exclude="/tmp" \
      --exclude="/dev" \
      -cf "${data_dir}/images/${image_name}.tar.gz" .
    if [ -d "${data_dir}/images/${image_name}" ]; then
        chflags -R noschg "${data_dir}/images/${image_name}"
        rm -fr "${data_dir}/images/${image_name}"
    fi
}

commit_image
