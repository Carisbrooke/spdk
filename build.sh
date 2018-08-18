#!/bin/bash

./configure --with-dpdk=/root/dpdk/dpdk/x86_64-native-linuxapp-gcc
make
if [ $? -eq 0 ]; then
        ldconfig
        echo "build ok"
else
        echo  "build failed"
fi

