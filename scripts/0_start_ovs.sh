#! /bin/bash

# starts ovsdb server and ovs

# start the config server
ovsdb-tool create /usr/local/etc/openvswitch/conf.db /usr/local/share/openvswitch/vswitch.ovsschema
ovsdb-server --remote=punix:/usr/local/var/run/openvswitch/db.sock --remote=db:Open_vSwitch,Open_vSwitch,manager_options --remote=ptcp:6632 --detach > /dev/null &

# mount huge pages
mkdir /dev/hugepages
mount -t hugetlbfs nodev /dev/hugepages

# start ovs
export DB_SOCK=/usr/local/var/run/openvswitch/db.sock
ovs-vswitchd --dpdk -c 0x0F -n 4 -m 2048 -- unix:$DB_SOCK --pidfile
