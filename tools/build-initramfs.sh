#!/bin/bash
# Build the Linux initramfs for the Duo 2 (busybox + USB-networking init)
set -e
cd "$(dirname "$0")"

ROOT=initramfs-root
rm -rf "$ROOT"/bin "$ROOT"/sbin "$ROOT"/usr "$ROOT"/etc
mkdir -p "$ROOT"/bin "$ROOT"/sbin "$ROOT"/dev "$ROOT"/proc "$ROOT"/sys "$ROOT"/tmp "$ROOT"/run "$ROOT"/etc

# busybox + applet symlinks
cp busybox-src/busybox "$ROOT/bin/busybox"
chmod 755 "$ROOT/bin/busybox"
for a in sh ash mount umount ifconfig ip ipconfig telnetd ls head cat echo sleep ln mkdir mknod chmod grep sed df dmesg hostname udhcpc init halt reboot poweroff; do
    ln -sf busybox "$ROOT/bin/$a"
done
ln -sf /bin/busybox "$ROOT/init_busybox" 2>/dev/null || true

# minimal etc
echo "127.0.0.1 localhost" > "$ROOT/etc/hosts"
echo "nameserver 8.8.8.8" > "$ROOT/etc/resolv.conf"

chmod +x "$ROOT/init"

# pack
( cd "$ROOT" && find . -print0 | cpio --null -o -H newc 2>/dev/null | gzip ) > ramdisk-usbnet.cpio.gz
echo "built ramdisk-usbnet.cpio.gz ($(du -h ramdisk-usbnet.cpio.gz | cut -f1))"