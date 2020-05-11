#!/bin/sh
#
set -e
set -x

tar -C "${1}/${2}" --exclude="/tmp" \
    --exclude="/dev" \
    -cf "${3}/images/${4}.tar.gz" .
devfs rule -s 5000 delset
for i in `jot $5 0`; do
    umount "${1}/${i}/dev"
done
for i in `jot $5 0`; do
    umount "${1}/${i}"
    chflags -R noschg "${1}/${i}" 
    rm -fr "${1}/${i}"
done
