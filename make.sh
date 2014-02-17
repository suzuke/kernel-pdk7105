#!/bin/bash
set -o errexit
[ $# -ne 1 ] && echo "Usage: make.sh <kernel/all/menu>" && exit 1
if ! which sh4-linux-gcc >/dev/null 2>&1 ; then
  echo "Err: sh4-linux-gcc not exist" && exit 1
fi
go() {
  echo "*** $*"
  "$@" || exit 1
}
get_fossil_version() { # <localversion-fossil>
  if [ -x /usr/bin/fossil -a -r manifest.uuid ] ; then
    local _id=$(cat manifest.uuid)
    echo "-${_id:0:4}" > $1
  fi
}

jobs=`grep processor  /proc/cpuinfo |wc -l`
jobs=$((jobs+1))
echo "jobs=$jobs"

get_fossil_version localversion-fossil

XFLAGS="ARCH=sh CROSS_COMPILE=sh4-linux- -j$jobs"
#XFLAGS="ARCH=sh CONFIG_DEBUG_SECTION_MISMATCH=y CROSS_COMPILE=sh4-linux- -j$jobs"

mv .config .config.old || true
if [ ! -r .config ] ; then
  if [ -r kernel.config ] ; then
    echo "copy previous kernel config"
    cp kernel.config .config
  else
    make $XFLAGS pdk7105_defconfig
  fi
fi
if [ "$1" = "all" -o "$1" = "kernel" ] ; then
  go make $XFLAGS oldconfig
elif [ "$1" = "menu" ] ; then
  go make $XFLAGS menuconfig
  cp .config kernel.config
else
  echo "Usage: make.sh <kernel/all/menu>" && exit 1
fi

echo '***' CFLAGS="-Wall -Wformat -m -pipe -O3 -ffast-math" make $XFLAGS vmlinux
CFLAGS="-Wall -Wformat -m -pipe -O3 -ffast-math" make $XFLAGS vmlinux

# old method to create vmlinux.ub
#  # go sh4-linux-strip vmlinux # it produce the same file
#  go sh4-linux-objcopy -O binary vmlinux vmlinux.bin
#  go ls -l vmlinux.bin
#  go gzip --best --force vmlinux.bin
#  go ls -l vmlinux.bin.gz
#  #name=$(basename $(realpath kernel.config) .config)
#  #name=twpda
#  #go rm -f vmlinux.ub.$name
#  # ref: http://stlinux.com/u-boot/mkimage/kernel-images
#  eaddr=$(sh4-linux-objdump -f vmlinux | grep '^start address ' | awk '{print $3}')
#  aaddr=$(sh4-linux-objdump -h vmlinux | grep .empty_zero_page | awk '{print $4}')
#  go mkimage -A sh -O linux -T kernel -C gzip \
#    -a $aaddr -e $eaddr \
#    -n "Linux 2.6" -d vmlinux.bin.gz vmlinux.ub
#  go ls -l vmlinux.ub

go make $XFLAGS uImage
go ls -l arch/sh/boot/uImage.gz
if [ "$1" = kernel ] ; then
  exit 0
fi
#go fakeroot tar -cJf sh4twbox-kernel.txz vmlinux.ub
#ls -l *.txz
rm -rf lib/modules
go make $XFLAGS modules

echo '***' INSTALL_MOD_PATH=. make $XFLAGS modules_install
INSTALL_MOD_PATH=. make $XFLAGS modules_install > modules_install.log 2>&1
if tail -1 modules_install.log | grep modules.builtin > /dev/null ; then
  # http://archlinuxarm.org/forum/viewtopic.php?t=2376
  echo "*** try to fix modules.builtin: No such file or directory"
  (cd lib/modules/* ; find . | grep ko | cut -f 2,3 -d \. | cut -f 2- -d \/ > modules.builtin)
  INSTALL_MOD_PATH=. make $XFLAGS modules_install || rc=1
fi
ls -ld lib/modules
ls -l arch/sh/boot/uImage.gz

# vim:et sw=2 ts=2 ai
