#!/bin/sh

echo "echo 1024 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages" > .echo_tmp
sudo sh .echo_tmp
rm -f .echo_tmp

echo "echo 1024 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages" > .echo_tmp
sudo sh .echo_tmp
rm -f .echo_tmp

sudo umount /mnt/huge
sudo rm -R /mnt/huge

sudo mkdir -p /mnt/huge
grep -s '/mnt/huge' /proc/mounts > /dev/null
if [ $? -ne 0 ] ; then
    sudo mount -t hugetlbfs nodev /mnt/huge
fi

sudo modprobe uio_pci_generic

sudo $HOME/dpdk/usertools/dpdk-devbind.py -b uio_pci_generic 06:00.0 06:00.1 06:00.2 06:00.3
