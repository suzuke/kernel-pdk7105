#!/bin/bash
set -e errexit

rm -rf cscope lib/modules modules_install.log
make distclean
rm -f vmlinux.ub \
arch/sh/boot/uImage \
arch/sh/boot/uImage.gz \
arch/sh/boot/vmlinux.bin \
arch/sh/boot/vmlinux.bin.gz \
arch/sh/kernel/vmlinux.lds \
localversion-fossil

#sh4twbox-kernel-*.txz
