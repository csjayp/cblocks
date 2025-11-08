#!/bin/sh
#

. /etc/rc.conf

if [ -z "$cblockd_data_dir" ] || [ -z "$cblockd_fs" ]; then
    echo ERROR: set cblockd rc.conf variables
    exit 1
fi
