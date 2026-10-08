#!/bin/sh
#
# Copyright (c) 2026 Christian S.J. Peron
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
# Remove image tags: cmd_image_remove.sh -R DATA_DIR -- NAME[:TAG] ...
# A missing tag means "latest". The image itself is deleted with its
# last tag, unless a running instance or build is using it.
#
. "$(dirname "$0")/common.sh"
data_dir=""

remove_tag()
{
    name="${1%%:*}"
    tag="latest"
    case "$1" in
    *:*)
        tag="${1#*:}"
        ;;
    esac
    # The names come from the client. Keep them inside the images
    # directory.
    case "$name:$tag" in
    -*|*/*|:*|*:|*:*:*)
        echo "${1}: invalid image name"
        return 1
        ;;
    esac
    link="${data_dir}/images/${name}:${tag}"
    if [ ! -h "$link" ]; then
        echo "${name}:${tag}: no such image"
        return 1
    fi
    image=$(readlink "$link")
    for other in $(symlinks); do
        if [ "$other" != "$link" ] && [ "$(readlink "$other")" = "$image" ]; then
            rm "$link"
            echo "Untagged ${name}:${tag}"
            return 0
        fi
    done
    if image_in_use "$image"; then
        echo "${name}:${tag}: image is in use, not removed"
        return 1
    fi
    rm "$link"
    remove_image "$image" || return 1
    echo "Removed ${name}:${tag}"
}

while getopts "R:" opt; do
    case $opt in
        R)
            data_dir="$OPTARG"
            ;;
        *)
            exit 1
            ;;
    esac
done
shift $((OPTIND - 1))

if [ ! "$data_dir" ]; then
    echo "Must specify cblock data directory -R"
    exit 1
fi

status=0
for arg in "$@"; do
    remove_tag "$arg" || status=1
done
exit $status
