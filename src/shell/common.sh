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

path_to_vol()
{
    printf "%s" "$1" | sed -E "s,^/(.*),\1,g"
}

images()
{
    find "${data_dir}/images" \
      -mindepth 1 -maxdepth 1 -type d
}

symlinks()
{
    find "${data_dir}/images" \
      -mindepth 1 -maxdepth 1 -type l
}

# Delete an image directory (or dataset) under ${data_dir}/images. The
# caller makes sure no tag points at it and nothing is using it.
remove_image()
{
    case $CBLOCK_FS in
    zfs)
        zfs destroy -r "$(path_to_vol "$1")"
        rm -fr "$1"
        ;;
    ufs)
        chflags -R noschg "$1"
        rm -fr "$1"
        ;;
    *)
        echo "No match on CBLOCK_FS"
        return 1
        ;;
    esac
}

# Succeeds if a running instance or build is using the image in $1.
# Launches and builds mount the image's root as the lower layer of a
# unionfs (ufs), or clone a snapshot of its dataset (zfs).
image_in_use()
{
    case $CBLOCK_FS in
    zfs)
        zfs list -H -o clones -t snapshot -d 1 "$(path_to_vol "$1")" | \
          grep -qv '^-$'
        ;;
    ufs)
        mount -p | awk -v dir="$1/root" '
            { n = split($1, layer, ":")
              for (i = 1; i <= n; i++)
                  if (layer[i] == dir) found = 1 }
            END { exit !found }'
        ;;
    esac
}

ip_to_int()
{
    echo "$1" | awk -F. '{ print (($1 * 256 + $2) * 256 + $3) * 256 + $4 }'
}

# Print the IPv4 address used for traffic over the default route: the
# address on the default interface whose subnet contains the gateway,
# which is the one the kernel picks as the source address. Some clouds
# (GCE) put the address in a /32 that does not contain the gateway; in
# that case fall back to the interface's first (primary) address.
get_default_ip()
{
    route_info=$(route -n get default)
    gateway=$(echo "$route_info" | awk '/gateway:/ { print $2 }')
    netif=$(echo "$route_info" | awk '/interface:/ { print $2 }')
    first=""
    for addr in $(ifconfig "$netif" inet | awk '/inet / { print $2 "/" $4 }'); do
        ipv4=${addr%/*}
        mask=${addr#*/}
        if [ -z "$first" ]; then
            first=$ipv4
        fi
        case "$gateway" in
        *.*.*.*)
            if [ $(( $(ip_to_int "$ipv4") & mask )) -eq \
                 $(( $(ip_to_int "$gateway") & mask )) ]; then
                echo "$ipv4"
                return
            fi
            ;;
        esac
    done
    echo "$first"
}
