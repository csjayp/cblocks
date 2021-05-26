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
data_dir=""
obliterate="no"

get_vol()
{
    printf "%s" "$1" | sed -E "s,^/(.*),\1,g"
}

images()
{
    find "${data_dir}/images" \
      -mindepth 1 -maxdepth  1 -type d
}

symlinks()
{
    find "${data_dir}/images" \
      -mindepth 1 -maxdepth  1 -type l
}

do_image_purge()
{
    for image in $(images); do
        match="no"
        for link in $(symlinks); do
            target=$(readlink "$link")
            if [ "$target" = "$image" ]; then
                match="$link"
                break
            fi
            if [ "$obliterate" = "yes" ]; then
                rm $link
            fi
        done
        if [ "$obliterate" = "yes" ]; then
            match="no"
        fi
        if [ "$match" != "no" ]; then
            continue
        fi
        printf "Removing un-referenced image: %s\n" $(basename $image)
        case $CBLOCK_FS in
        zfs)
            vol=$(get_vol $image)
            zfs destroy -r "$vol"
            rm -fr "$vol"
            ;;
        ufs)
            chflags -R noschg "$image"
            rm -fr "$image"
            ;;
        *)
            echo "No match on CBLOCK_FS"
        esac
    done
}

while getopts "R:o" opt; do
    case $opt in
        o)
            echo "WARNING: You have selected to obliterate everything"
            echo -n "Are you sure you know what you are doing? (yes/no): "
            read answer
            case $answer in
            yes|YES)
                obliterate="yes"
                ;;
            *)
                exit 0
                ;;
            esac
            ;;
        R)
            data_dir="$OPTARG"
            ;;  
    esac
done

if [ ! "$data_dir" ]; then
    echo "Must specify cblock data directory -R"
    exit 1
fi

do_image_purge
