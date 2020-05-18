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
    tar -C "${build_root}/${build_index}" --exclude="/tmp" \
      --exclude="/dev" \
      -cf "${data_dir}/images/${image_name}.tar.gz" .
}

cleanup()
{
    for i in `jot ${n_stages} 0`; do
        umount "${build_root}/${i}/dev"
    done
    for i in `jot ${n_stages} 0`; do
        umount -f "${build_root}/${i}"
        chflags -R noschg "${build_root}/${i}" 
        rm -fr "${build_root}/${i}"
    done
    rm -fr ${build_root}.*
    rm -fr "${build_root}"
    devfs rule -s 5000 delset
}

commit_image
cleanup
