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
data_dir=$1

humanize()
{
    #
    # On FreeBSD, test integer operations operate on uintmax_t which is
    # 64 bits (on 64 bit architectures). They are also using strtoimax
    # to convert the operands which should give us enough room to process
    # sizes for multi-terrabyte images.
    #
    # Note using IEC SI (1000 bytes per kilobyte)
    #
    val="$1"
    if [ "$val" -lt 1000 ]; then
        printf "${val}B"
        return
    fi
    if [ "$val" -ge 1000 ] && [ "$val" -lt 1000000 ]; then
        quo=$(echo "$val / 1000" | bc -l)
        printf "%.2fK" "${quo}"
        return
    fi
    if [ "$val" -ge 1000000 ] && [ "$val" -lt 1000000000 ]; then
        quo=$(echo "$val / 1000000" | bc -l)
        printf "%.2fM" "${quo}"
        return
    fi
    if [ "$val" -ge 1000000000 ]; then
        quo=$(echo "$val / 1000000000" | bc -l)
        printf "%.2fG" "${quo}"
    fi
}

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

ret_bytes_transferred()
{
    cat $1 | grep -F 'bytes transferred' | awk '{ print $1 }'
}

print_image_summary()
{
    printf "%-16.16s %-16.16s %12.12s %-20.20s\n" "IMAGE" "TAG" "SIZE" "CREATED"
    for link in $(symlinks); do
        _base=$(basename $link)
        _tag=$(echo $_base | cut -f2 -d:)
        _img=$(echo $_base | cut -f1 -d:)
        target=$(readlink $link)
        _size="Unknown"
        if [ -f "$target/TOTALS" ]; then
            _int=$(ret_bytes_transferred "$target/TOTALS")
            _size=$(humanize $_int)
        fi
        eval $(stat -s $target)
        IFS=$'\n'
        _date_string=`date -r "$st_birthtime" "+%Y-%m-%d %H:%M:%S"`
        printf "%-16.16s %-16.16s %12.12s %20.20s\n" $_img $_tag \
          $_size $_date_string
    done
}

while getopts "R:t:i:n:" opt; do
    case $opt in
        R)
            data_dir="$OPTARG"
            ;;  
    esac
done

print_image_summary
