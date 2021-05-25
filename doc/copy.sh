#!/bin/sh
#
BINS="cp fetch ln mkdir mktemp rm sh tar"
ETCS="protocols resolv.conf services"
CAPATH="/usr/local/share/certs/ca-root-nss.crt"

rm -fr libmap.conf

chflags -R noschg .

mkdir -p libexec
mkdir -p lib
mkdir -p bin
mkdir -p etc

cp /libexec/ld-elf.so.1 libexec
cp $CAPATH etc/

for etc_file in $ETCS; do
    cp "/etc/$etc_file" etc/
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
