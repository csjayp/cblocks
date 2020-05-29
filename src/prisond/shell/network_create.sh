#!/bin/sh
#
data_root=""
type=""
netif=""
net_name=""

get_lowest_bridge_unit()
{
    iflist=`ifconfig -l`
    NOMATCH="TRUE"
    for unit in `jot 1024 0`; do
        ifconfig "bridge${unit}" create 2>/dev/null
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
    printf "${net_name}"
}

while getopts "R:t:i:n:" opt; do
    case $opt in
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
case "$type" in
    bridge)
        bridgeif=`do_bridge_setup`
        if [ $? -ne 0 ]; then
            exit 1
        fi
        ;;
    *)
        echo "Un-supported network type $type"
        exit 1
        ;;
esac

echo "Bridge $bridgeif configured and ready for containers"
ifconfig "$bridgeif"
