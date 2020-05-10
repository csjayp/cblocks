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
stage_name=""
if [ "$7" ]; then
    stage_name=$7
fi

stage_deps_dir=""

get_default_ip()
{
    netif=`route get www.fastly.com | grep -F 'interface:' | awk '{ print $2 }'`
    ipv4=`ifconfig ${netif} | egrep "inet " | tail -n 1 | awk '{ print $2 }'`
    echo ${ipv4}
}

cleanup()
{
    for stage in $stage_deps; do
        umount "${stage_deps_dir}/${stage}"
    done
    umount "${build_root}/${stage_index}/dev"
    devfs rule -s 5000 delset
    if [ "$stage_name" ]; then
        echo "Importing image ${stage_name}"
        tar -C "${build_root}/${stage_index}" --xz -cf "${data_dir}/images/${stage_name}.txz" .
    fi
    umount "${build_root}/${stage_index}"
    chflags -R noschg "${build_root}/${stage_index}"
    rm -fr "${build_root}/${stage_index}"
}

bind_devfs()
{
    mount -t devfs devfs "${build_root}/${stage_index}/dev"
    devfs -m "${build_root}/${stage_index}/dev" ruleset 5000
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
    devfs -m "${build_root}/${stage_index}/dev" rule applyset
}

bootstrap()
{

    if [ ! -d "${build_root}/${stage_index}" ]; then
        echo "Prison daemon didn't create the stage root?"
        exit 1
    fi
    base_root="${data_dir}/images/${base_container}"
    echo "Checking for the presence of base image ${base_container}"
    if [ ! -d "${base_root}" ]; then
        #
        # Check to see if we have an ephemeral build stage
        if [ ! -h "${build_root}/images/${base_container}" ]; then
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
            echo "Ephemeral image found from previous stage"
            base_root="${build_root}/images/${base_container}"
        fi
    fi
    echo "Image located and ready for use"
    echo "Underlaying image ${base_container}"

    mount -t unionfs -o below ${base_root} ${build_root}/${stage_index}
    #
    # Create a upper directory for the overlay. This will contain all the
    # modified and new files.
    #
    #upper_overlay="${build_root}/${stage_index}.upper"
    #mkdir "${upper_overlay}"
    #unionfs -o direct_io \
    #  -o hard_remove \
    #  -o sync_read \ 
    #  -o cow \
    #  -o allow_other,use_ino \
    #  "${upper_overlay}=RW:${base_root}" \
    #  "${build_root}/${stage_index}"
    #
    # Mount previous stages for COPY --FROM if required.
    #
    stage_deps_dir=`mktemp -d "${build_root}/${stage_index}/tmp/deps.XXXXXXX"`
    for stage in $stage_deps; do
        upper_overlay="${stage_deps_dir}/${stage}.upper"
        mkdir "${stage_deps_dir}/${stage}"
        mount -t unionfs -o below -o ro "${build_root}/${stage}" "${stage_deps_dir}/${stage}"
        #mkdir "${upper_overlay}"
        #unionfs -o direct_io \
        #  -o hard_remove \
        #  -o sync_read \ 
        #  -o cow \
        #  -o allow_other,use_ino \
        #  "${upper_overlay}=RW:${build_root}/${stage}" \
        #  "${stage_deps_dir}/${stage}"
    done
    echo "Creating initial work directory in stage ${stage_index} environment"
    stage_work_dir=`mktemp -d "${build_root}/${stage_index}/tmp/XXXXXXXX"`
    echo "Extracting build context..."
    tar -C "${stage_work_dir}" -zxf "${build_context}"
    chmod +x "${build_root}.${stage_index}.sh"
    cp -p "${build_root}.${stage_index}.sh" "${build_root}/${stage_index}/prison-bootstrap.sh"
    VARS="${build_root}/${stage_index}/prison_build_variables.sh"
    stage_tmp_dir=`echo ${stage_work_dir} | sed s,${build_root}/${stage_index},,g`
    stage_deps_mount=`echo "${stage_deps_dir}" | sed "s,${build_root}/${stage_index},,g"`
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
#cleanup
