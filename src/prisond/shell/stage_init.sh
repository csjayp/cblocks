#!/bin/sh
#
set -e 
#set -x 

build_root=$1
stage_index=$2
base_container=$3
data_dir=$4
build_context=$5
stage_deps=$6


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
    umount "${build_root}/${stage_index}"
    chflags -R noschg "${build_root}/${stage_index}"
    rm -fr "${build_root}/${stage_index}"
    devfs rule -s 5000 delset
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
    echo "Underlaying image ${base_container}"
    mount -t unionfs -o noatime -o below "${data_dir}/images/${base_container}" \
        "${build_root}/${stage_index}"
    #
    # Mount previous stages for COPY --FROM if required.
    #
    stage_deps_dir=`mktemp -d "${build_root}/${stage_index}/tmp/deps.XXXXXXX"`
    for stage in $stage_deps; do
        mkdir "${stage_deps_dir}/${stage}"
        mount -t nullfs "${build_root}/${stage}" "${stage_deps_dir}/${stage}"
    done
    echo "Creating initial work directory in stage ${stage_index} environment"
    stage_work_dir=`mktemp -d "${build_root}/${stage_index}/tmp/XXXXXXXX"`
    echo "Extracting build context..."
    tar -C "${stage_work_dir}" -zxf "${build_context}"
    chmod +x "${build_root}.${stage_index}.sh"
    mv "${build_root}.${stage_index}.sh" "${build_root}/${stage_index}"

    VARS="${build_root}/${stage_index}/prison_build_variables.sh"
    stage_tmp_dir=`echo ${stage_work_dir} | sed s,${build_root}/${stage_index},,g`
    echo "stage_tmp_dir=${stage_tmp_dir}" >> $VARS
    echo "build_root=${build_root}" >> $VARS
    echo "stage_work_dir=${stage_work_dir}" >> $VARS
    echo "stage_index=${stage_index}" >> $VARS
    stage_deps_mount=`echo "${stage_deps_dir}" | sed "s,${build_root}/${stage_index},,g"`
    echo "stages=${stage_deps_mount}" >> $VARS
    bind_devfs
}

init_build()
{
    name=`basename ${build_root}`
    ip4=`get_default_ip`
    jail -c host.hostname=${name} \
        ip4.addr=${ip4} \
        name=${name} \
        osrelease=12.1-RELEASE \
        path="${build_root}/${stage_index}" \
        command="/${name}.${stage_index}.sh"
    echo "Exited from jail context ${name}"
    echo "Removing ${name}"
    jail -R "${name}"
    sync
}

bootstrap
init_build
#cleanup
