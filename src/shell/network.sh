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
data_root=""
type=""
netif=""
net_name=""
net_mask=""
op=""

get_lowest_bridge_unit()
{
    iflist=$(ifconfig -l)
    NOMATCH="TRUE"
    for unit in $(jot 1024 0); do
        ifconfig "bridge${unit}" create up 2>/dev/null
        if [ "$?" -ne 0 ]; then
            continue
        fi 
        printf "$unit"
        NOMATCH="FALSE"
        break
    done
    if [ "$NOMATCH" = "TRUE" ]; then
        echo "Exhausted bridge list. Giving up"
        return 1
    fi
}

do_nat_setup()
{
    if [ ! "$net_mask" ]; then
        echo "Network mask must be specified in a.b.c.d/cidr format"
        exit 1
    fi
    iflist=$(ifconfig -l)
    NOMATCH="TRUE"
    for n in $iflist; do
        if [ "$n" = "cblock0" ]; then
            NOMATCH="FALSE"
            break
        fi
    done
    if [ "$NOMATCH" = "TRUE" ]; then
        # NB: don't assume lo1 is free, we will need a unit lookup here too.
        ifconfig lo1 create up
        ifconfig lo1 name cblock0
    fi
    # NB: we should be doing these under a lockf to avoid races
    while read ln; do
        n_type=$(echo "$ln" | awk -F, '{ print $1 }')
        n_name=$(echo "$ln" | awk -F, '{ print $2 }')
        n_netif=$(echo "$ln" | awk -F, '{ print $3 }')
        if [ "$n_name" = "$net_name" ]; then
            echo "Network \'$net_name\' is already defined"
            echo "        $ln"
            exit 1
        fi
    done < ${data_root}/networks/network_list
    if [ $(echo $net_mask | grep -c -F :) = "1" ]; then
        ip_version="6"
    else
        ip_version="4"
    fi
    echo "nat,${net_name},$netif,$net_mask,$ip_version" >> \
      ${data_root}/networks/network_list
}

do_bridge_setup()
{
    unit=$(get_lowest_bridge_unit)
    if [ $? -ne 0 ]; then
        echo "Failed to calculate lowest bridge unit"
        exit 1
    fi
    ifconfig bridge"${unit}" addm "$netif" >/dev/null
    if [ $? -ne 0 ]; then
        ifconfig bridge"${unit}" destroy
        echo "Failed to add root netif $netif to bridge $net_name"
        exit 1
    fi
    ifconfig "bridge${unit}" name "${net_name}" >/dev/null
    if [ $? -ne 0 ]; then
        ifconfig bridge"${unit}" destroy 
        echo "Failed to rename bridge interface bridge${unit} to ${net_name}"
        exit 1
    fi
    echo "bridge,${net_name},$netif" >> ${data_root}/networks/network_list
    printf "${net_name}"
}

net_in_use()
{
    while read ln; do
        _name=$(echo $ln | cut -f 4 -d:)
        if [ "$_name" = "$net_name" ]; then
            return 1
        fi
    done < $data_root/networks/cur
    return 0
}

net_destroy()
{
    net_in_use
    if [ $? -ne 0 ]; then
        echo Network is currently in use
        exit 1
    fi
    netdb=$(mktemp)
    while read ln; do
        _type=$(echo $ln | cut -f 1 -d,)
        _name=$(echo $ln | cut -f 2 -d,)
        if [ "$_name" != "$net_name" ]; then
            echo $ln >> $netdb
            continue
        fi
        case $_type in
        bridge)
            ifconfig $_name destroy
            ;;
        esac
    done < ${data_root}/networks/network_list
    rm ${data_root}/networks/network_list
    mv $netdb ${data_root}/networks/network_list
}

net_list()
{
    printf "%7.7s %10.10s  %5.5s   %-40.40s\n" "TYPE" "NAME" "NETIF" "NET"
    while read ln; do
        type=$(echo $ln | cut -f 1 -d,)
        name=$(echo $ln | cut -f 2 -d,)
        netif=$(echo $ln | cut -f 3 -d,)
        net="-"
        if [ "$type" != "bridge" ]; then
            net=$(echo $ln | cut -f 4 -d,)
        fi
        printf "%7.7s %10.10s  %5.5s   %-40.40s\n" $type $name $netif $net
    done < ${data_root}/networks/network_list
}

net_create()
{
    case "$type" in
        bridge)
            bridgeif=$(do_bridge_setup)
            if [ $? -ne 0 ]; then
                exit 1
            fi
            echo "Bridge $bridgeif configured and ready for containers"
            ifconfig "$bridgeif"
            ;;
        nat)
            do_nat_setup
            ;;
        *)
            echo "Un-supported network type $type"
            exit 1
            ;;
    esac
}

while getopts "o:m:R:t:i:n:" opt; do
    case $opt in
        o)
            case $OPTARG in
            list)
                op="list"
                ;;
            create)
                op="create"
                ;;
            destroy)
                op="destroy"
                ;;
            *)
                echo FATAL: invalid operation $OPTARG
                ;;
            esac
            ;;
        m)
            net_mask=$OPTARG
            ;;
        R)
            data_root="$OPTARG"
            ;;
        t)
            type="$OPTARG"
            ;;
        i)
            netif="$OPTARG"
            ;;
        n)
            net_name="$OPTARG"
            ;;
    esac
done

if [ ! "$op" ]; then
    echo "Must specify operation"
    exit 1
fi

if [ ! -f "${data_root}"/networks/network_list ]; then
    touch "${data_root}"/networks/network_list
fi

case $op in
create)
    if [ ! "$type" ] || [ ! "$netif" ] || [ ! "$net_name" ]; then
        echo "Network type, name and parent interface must be specified"
        exit 1
    fi
    net_create
    ;;
destroy)
    net_destroy
    ;;
list)
    net_list
    ;;
esac
