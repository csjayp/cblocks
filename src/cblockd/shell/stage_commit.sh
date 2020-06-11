#!/bin/sh
#
set -e

build_root=$1
build_index=$2
data_dir=$3
image_name=$4
n_stages=$5
instance="$6"
fim_spec_mode="$7"
build_tag="$8"

path_to_vol()
{
    echo -n "$1" | sed -E "s,^/(.*),\1,g"
}

commit_image()
{
    if [ -h "${build_root}/${build_index}/root/cellblock-root-ptr" ]; then
        dir=$(readlink "${build_root}/${build_index}/root/cellblock-root-ptr")
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
        printf "\033[1m--\033[0m %s\n" \
          "Generating cryptographic checksums for container image files"
        cd "${src}"
        mtree -c -K sha256 -p . > FIM.spec
    fi
    case $CBLOCK_FS in
    zfs)
        nvol=$(path_to_vol "${data_dir}/images/${image_name}.${instance}")
        zfs create "${nvol}"
        ;;
    ufs)
        mkdir "${data_dir}/images/${image_name}.${instance}"
        ;;
    esac
    #bytes=`du -sk "${src}" | awk '{ print $1 }'`
    lockf -k "${data_dir}/images/${image_name}.tar.zst" \
      tar -C "${src}" --exclude="/tmp" \
      --no-xattrs \
      --exclude="/dev" \
      -cf - . | \
    tar -xpf - -C "${data_dir}/images/${image_name}.${instance}"
    # NB: we need to do this atomically
    #
    if [ -h "${data_dir}/images/${image_name}:${build_tag}" ]; then
        rm "${data_dir}/images/${image_name}:${build_tag}"
    fi
    ln -s "${data_dir}/images/${image_name}.${instance}" \
        "${data_dir}/images/${image_name}:${build_tag}"
}

commit_image
