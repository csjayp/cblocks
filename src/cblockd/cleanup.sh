#!/bin/sh
#
df | grep deps | awk '{ print $6 }' | xargs -n 1 sudo umount -f 
df | grep test | grep dev | awk '{ print $6 }' | xargs -n 1 sudo umount 
df | grep below | awk '{ print $6 }' | xargs -n 1 sudo umount -f
df | grep devfs | grep ssd |awk '{ print $6 }' | xargs -n 1 sudo umount  -f
sudo devfs rule -s 5000 delset

df | grep -F 'instances/' | awk '{ print $1 }'  | xargs -n 1 sudo zfs destroy -rR
df | grep fuse | awk '{ print $6 }' | xargs -n 1 sudo umount -f 
