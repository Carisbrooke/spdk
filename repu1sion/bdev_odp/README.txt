run
-----
ODP_HW_TIMESTAMPS=1 SPDK_NOINIT=1 ./bdev_odp
ODP_HW_TIMESTAMPS=1 SPDK_NOINIT=1 ./bdev_odp [rwb]	- read, write, both (write by default)
ODP_HW_TIMESTAMPS=1 SPDK_NOINIT=1 ./bdev_odp b

dump
-----
tcpdump --time-stamp-precision=nano -r dump.pcap

free pages
-----
hugefree.sh

use 4.9.X gcc
-----
PATH=/opt/rh/devtoolset-3/root/usr/bin:$PATH

coredump
-----
ulimit -c unlimited
sysctl -w kernel.core_pattern=core_%p_%t

