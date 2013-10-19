kernel-pdk7105 - NextVod's Kernel
========================================================================

Short history of this kernel
----------------------------
This section provides some hints when you want to enhance/trace or debug this kernel.

 1. [STLinux][st] porting [linux 2.6.23][2.6.23] to [STLinux 2.3 v2.6.23.17_stm23_0125][st23]
 2. NextVOD's source: [SMIT][smit] porting STLinux to [NextVod GB620_PDK7105_A18B][smitkernel]
 3. [suzuke][suzuke] porting [STLinux 2.4 v2.6.32.59][st24] to [kernel-pdk7105][suzukekernel]
 4. [TWPDA][twpda] fork [a branch kernel][twkernel] for sh4twbox. ([developer page][twkernelsite])

Graph of upstream sources:

 * linux: [v2.6.23][2.6.23], [v2.6.32][2.6.32], [v3.11.6][3.11.6]
 * STLinux host-kernel:
  * stlinux 2.3: [v2.6.23.17_stm23_0125][st23]
  * stlinux 2.4: [v2.6.32.59_stm24_0211][st24]

[2.6.23]: https://www.kernel.org/pub/linux/kernel/v2.6/linux-2.6.23.tar.xz
[st23]: http://download.stlinux.com/pub/stlinux/2.3/updates/SRPMS/stlinux23-host-kernel-source-sh4-2.6.23.17_stm23_0125-125.src.rpm
[smitkernel]: http://code.google.com/p/nexttv-mod/downloads/list
[suzukekernel]: https://github.com/suzuke/kernel-pdk7105
[smit]: http://www.smit.com.cn/
[st24]: http://download.stlinux.com/pub/stlinux/2.4/updates/SRPMS/stlinux24-host-kernel-source-sh4-2.6.32.59_stm24_0211-211.src.rpm
[suzuke]: https://sites.google.com/site/debiansh4/
[2.6.32]: https://www.kernel.org/pub/linux/kernel/v2.6/linux-2.6.32.tar.xz
[3.11.6]: https://www.kernel.org/pub/linux/kernel/v3.x/linux-3.11.6.tar.xz
[st]: http://stlinux.com/
[twpda]: http://www.twpda.com
[twkernel]: https://github.com/dlintw/kernel-pdk7105/tree/twpda
[twkernelsite]: http://www.twpda.com/2012/10/nextvod-compile-kernel.html

Build
=====
Install [STLinux 2.4 developer environment](http://stlinux.com/install)

    . ./env24.sh # stlinux 2.4

    ./make.sh kernel # build kernel only
    ./make.sh all  # build kernel & modules
    ./make.sh menu # or, launch menu to choose options and build all

    make cscope # [optional], build cscope files for trace source code

    # for source code control
    ./clean.sh

Some hints for porting
======================
Study SMIT code
---------------

Search possible keyword smit gonjia wgzhu smit\_ in [smit kernel][smitkernel]

* old kernel
 * drivers/char/spi/spi25pxx.h (smit\_)
 * drivers/usb/storage/usb.h (smit)
 * drivers/usb/storage/usb.c (smit)
 * arch/sh/kernel/cpu/sh4/setup-stx7105.c (smit)
 * arch/sh/boards/st/pdk7105/setup.c (smit)

Extract src.rpm from STLinux
============================

Possible solutions:

 * [using source rpm][1]
 * [convert to deb by alien][2]
 * [build in mach chroot environment][3]
 * [simple extract][4]

[1]: http://vdt.cs.wisc.edu/internal/native/using-srpm.html
[2]: https://wiki.debian.org/Alien
[3]: http://www.howtoforge.com/building-rpm-packages-in-a-chroot-environment-using-mach
[4]: http://www.cyberciti.biz/tips/how-to-extract-an-rpm-package-without-installing-it.html

We use simple extract method.

    mkdir r ; cd r # make a workind directory
    rpm2cpio foo.src.rpm | cpio -idmv
    tar xf foo.tar.bz2 # extract the upstream code
    for f in *.patch.gz ; do gzip -d $f ; done
    # ref foo.spec, we know patch -p1 to patch the source
    cd foo
    for f in ../*.patch ; do patch -p1 < $f ; done

vim tips for tracing code
-------------------------

    # yaourt -S vim-cscope
    ./cscope.sh # rebuild cscope db for limited files
in vim

    # support 'cscope' command
    :cs add cscope
    :cs reset # whenever rebuild the cscope db
    # support 'mak' command
    :set makeprg=$PWD/make.sh
    :mak  # build and show line on error message

USB mis-order problem
---------------------

This issue require use stlinux24x7105_configure_usb() to solve.

1. arch/sh/boards/mach-pdk7105/setup.c enable config_usb(0) only.
2. patch drivers/usb/storage/usb.c and enable 2nd USB slot later.
3. This patch will cause the following warning.  That's point out
storage_probe() called a __init function, but itself is not.

WARNING: modpost: Found 1 section mismatch(es).
To see full details build your kernel with:
'make CONFIG_DEBUG_SECTION_MISMATCH=y'

ref: http://stackoverflow.com/questions/8832114/what-does-init-mean-in-this-linux-kernel-code

HISTORY
=======
* 2013/09/10 import kernel code
* 2013/10/19 release twpda patch
