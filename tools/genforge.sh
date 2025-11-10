#!/bin/sh
#
# Copyright (c) 2020 Christian S.J. Peron
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
BINS="cp fetch ln mkdir mktemp rm sh tar"
ETCS="protocols resolv.conf services"
CAPATH="/usr/local/share/certs/ca-root-nss.crt"

set -e

. /etc/rc.conf

./envcheck.sh

if [ ! -f "$CAPATH" ]; then
    echo ERROR: ca-root-nss certs are not installed
    echo HINT: run pkg install ca_root_nss
    exit 1
fi

if [ -z "$cblockd_data_dir" ] || [ -z "$cblockd_fs" ]; then
   echo ERROR: update /etc/rc.conf and set cblockd_enable cblockd_data_dir and cblockd_fs
   exit 1
fi

chflags -R noschg forge/ && rm -fr forge/
rm -fr forge.tgz && mkdir forge
cd forge

rm -fr libmap.conf

chflags -R noschg .

mkdir -p libexec
mkdir -p lib
mkdir -p bin
mkdir -p etc

cp /libexec/ld-elf.so.1 libexec
cp $CAPATH etc/

for etc_file in $ETCS; do
    cp -p "/etc/$etc_file" etc/
done

get_deps()
{
    bin="$1"
    ldd $(which $bin) | grep -F '=>' | awk '{ print $3 }'
}

dump_libs()
{
    for bin in $BINS; do
        libs=$(get_deps $bin)
        for lib in $libs; do
            echo $lib
        done
    done
}

get_unique_libs()
{
    dump_libs | sort | uniq
}

for lib in $(get_unique_libs); do
    dname=$(dirname $(echo $lib | sed -E s,^/,,g))
    libname=$(basename $lib)
    echo $libname /tmp/cblock_forge/lib/$libname >> libmap.conf
    cp -p $lib lib/
done

for bin in $BINS; do
    bpath=$(which $bin)
    dname=$(dirname $(echo $bpath | sed -E s,^/,,g))
    cp $bpath bin/
done

tar -czpf ../forge.tgz .
cblockd --${cblockd_fs}  --data-directory ${cblockd_data_dir} --create-forge ../forge.tgz
