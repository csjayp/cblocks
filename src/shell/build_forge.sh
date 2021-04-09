#!/bin/sh
#

set -e
set -x


dirs="bin etc lib libexec"
certs_path="/usr/local/share/certs/ca-root-nss.crt"
etcs="protocols resolv.conf services"
execs="cp fetch mkdir ln mktemp rm sh tar"

copy_files()
{
    for d in $dirs; do
        mkdir $d
    done
    if ! [ -r $certs_path ]; then
        echo "Could not find CA file. Maybe ca_root_nss is not installed?"
        exit 1
    fi
    cp $certs_path etc/
    for f in $etcs; do
        cp /etc/"$f" etc/
    done

    for bin in $execs; do
        epath=$(which $bin)
        stat $epath >/dev/null
        if [ "$?" -ne 0 ]; then
            echo "path for $bin not found"
            exit 1
        fi
        cp "$epath" bin/
        for lib in $(ldd bin/"$bin" | grep "^[[:space:]]" | awk '{ print $3 }'); do
            if ! [ -f lib/$(basename $lib) ]; then
                cp $lib lib/
            fi
        done
    done
    cp /libexec/ld-elf.so.1 libexec/
    mkdir tmp
}
cur=$(pwd)
scratch=$(mktemp -d /tmp/cblock_forge.XXXXX)
cd $scratch
copy_files
tar -C . -zcvf $cur/forge.$(date "+%s").tar.gz *
cd $cur
rm -fr $scratch
