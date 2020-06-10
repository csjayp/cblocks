#!/bin/sh 
#

data_dir=$1

do_instance_purge()
{
    for instance_path in `find ${data_dir}/instances -mindepth 1 -maxdepth  1 -type d`; do
        echo checking $instance_path
        instance=`basename "${instance_path}"`
        if [ -f "${data_dir}/locks/${instance}.pid" ]; then
            lockf -t 0 "${data_dir}/locks/${instance}.pid" true > /dev/null 2>&1
            if [ $? -ne 0 ]; then
                continue
            fi 
        fi
        echo Removing "$instance"
        chflags -R noschg "${instance_path}"/*
        rm -fr "${instance_path}"
    done
}

while getopts "R:t:i:n:" opt; do
    case $opt in
        R)
            data_dir="$OPTARG"
            ;;  
    esac
done

do_instance_purge
