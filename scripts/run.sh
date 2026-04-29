#!/bin/sh

# JQ8400 UART control needs the UART kernel module.
insmod /lib/modules/$(uname -r)/extra/uart_kmod.ko 2>/dev/null || true

chmod +x ./ssne_ai_demo
./ssne_ai_demo
