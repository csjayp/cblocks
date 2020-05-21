#!/bin/sh
#
set -e
set -x

build_root=$1
build_index=$2
data_dir=$3
image_name=$4
n_stages=$5
instance="$6"
fim_spec_mode="$7"

commit_image()
{
    if [ -h "${build_root}/${build_index}/root/cellblock-root-ptr" ]; then
        dir=`readlink "${build_root}/${build_index}/root/cellblock-root-ptr"`
        src="${build_root}/${build_index}/root/${dir}"
        #
        # If we have root pivoting step, make sure we copy the entry point
        # and entry point args from the original root.
        #
        if [ -f "${build_root}/${build_index}/ENTRYPOINT" ]; then
            cp "${build_root}/${build_index}/ENTRYPOINT" \
                "${build_root}/${build_index}/root/${dir}"
        fi
        if [ -f "${build_root}/${build_index}/ARGS" ]; then
            cp "${build_root}/${build_index}/ARGS" \
                "${build_root}/${build_index}/root/${dir}"
        fi
    else
        rm -fr "${build_root}/${build_index}/root/tmp/*"
        src="${build_root}/${build_index}"
    fi
    if [ "${fim_spec_mode}" = "ON" ]; then
        echo "-- Generating cryptographic checksums for container image files"
        cd "${src}"
        mtree -c -K sha256 -p . > FIM.spec
    fi
    tar -C "${src}" --exclude="/tmp" \
      --exclude="/dev" \
      -cf "${data_dir}/images/${image_name}.tar.gz" .
    if [ -d "${data_dir}/images/${image_name}" ]; then
        chflags -R noschg "${data_dir}/images/${image_name}"
        rm -fr "${data_dir}/images/${image_name}"
    fi
}

commit_image
