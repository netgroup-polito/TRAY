#! /bin/bash

FILE="/home/stack/Mauricio/vms/UbuntuServer.vdi"
NETWORK_CMD="-netdev user,id=hostnet0,hostfwd=tcp::2001-:22 -device rtl8139,netdev=hostnet0,id=net0"

qemu-system-x86_64 -vnc 0.0.0.0:1 \
-name Ubuntu -cpu host -smp 2 -machine accel=kvm,usb=off -m 4096 \
-drive file=$FILE $NETWORK_CMD $1 -monitor stdio
