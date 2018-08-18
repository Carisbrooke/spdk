run
-----
ODP_HW_TIMESTAMPS=1 SPDK_NOINIT=1 ./bdev_odp
ODP_HW_TIMESTAMPS=1 SPDK_NOINIT=1 ./bdev_odp [rwb]	- read, write, both (write by default)

dump
-----
tcpdump --time-stamp-precision=nano -r dump.pcap

free pages
-----
hugefree.sh
