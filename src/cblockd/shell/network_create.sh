#!/bin/sh
#
set -x

data_root=""
type=""
netif=""
net_name=""
net_mask=""

get_lowest_bridge_unit()
{
    iflist=`ifconfig -l`
    NOMATCH="TRUE"
    for unit in `jot 1024 0`; do
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
    iflist=`ifconfig -l`
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
        n_type=`echo $ln | awk -F: '{ print $1 }'`
        n_name=`echo $ln | awk -F: '{ print $2 }'`
        n_netif=`echo $ln | awk -F: '{ print $3 }'`
        if [ "$n_name" = "$net_name" ]; then
            echo "Network \'$net_name\' is already defined"
            echo "        $ln"
            exit 1
        fi
    done < ${data_root}/networks/network_list
    echo "nat:${net_name}:$netif:$net_mask" >> \
      ${data_root}/networks/network_list
}

do_bridge_setup()
{
    unit=`get_lowest_bridge_unit`
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
    echo "bridge:${net_name}:$netif" >> ${data_root}/networks/network_list
    printf "${net_name}"
}

while getopts "m:R:t:i:n:" opt; do
    case $opt in
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

if [ ! -f ${data_root}/networks/network_list ]; then
    touch ${data_root}/networks/network_list
fi

if [ ! "$type" ] || [ ! "$netif" ] || [ ! "$net_name" ]; then
    echo "Network type, name and parent interface must be specified"
    exit 1
fi

case "$type" in
    bridge)
        bridgeif=`do_bridge_setup`
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
