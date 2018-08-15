run
-----
ODP_HW_TIMESTAMPS=1 SPDK_NOINIT=1 ./bdev_odp

dump
-----
tcpdump --time-stamp-precision=nano -r dump.pcap

free pages
-----
hugefree.sh
