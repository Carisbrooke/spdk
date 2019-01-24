#!/bin/bash

./configure --with-dpdk=/root/dpdk/dpdk/x86_64-native-linuxapp-gcc --with-rbd --with-shared
make -j7
if [ $? -eq 0 ]; then
	make install
        ldconfig
        echo "build ok"
else
        echo  "build failed"
fi

