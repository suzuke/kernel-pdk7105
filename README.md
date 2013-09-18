kernel-pdk7105
==============

kernel 2.6.32-59 for pdk7105



==============
有鑑於網樂通釋出的kernel source過於老舊,

許多新的device都無法支援,所以移植了pdk7105至kernel 2.6.32.59,

在此將source code分享給大家, 希望有興趣的人可以藉此將網樂通做出更有趣的應用


make ARCH=sh CROSS_COMPILE=sh4-linux- pdk7105_defconfig
(可得到可以build成功的config)
make ARCH=sh CROSS_COMPILE=sh4-linux- menuconfig
make ARCH=sh CROSS_COMPILE=sh4-linux- vmlinux

裡面也包含一個autoDone.sh可以在編譯完成(make)後打包出vmlinux.ub



