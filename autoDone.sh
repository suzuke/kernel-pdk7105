sh4-linux-objcopy -O binary vmlinux vmlinux.bin
gzip --best --force vmlinux.bin
mkimage -A sh -O linux -T kernel -C gzip -a 0x80800000 -e 0x80801000 -n "Linux 2.6" -d vmlinux.bin.gz vmlinux.ub
