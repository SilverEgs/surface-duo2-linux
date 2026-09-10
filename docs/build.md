# Build & boot — reproducible guide

## Toolchain

- Bootlin `aarch64--glibc--stable-2024.02-1` (gcc 12.3.0) at `tools/toolchains/`.
  It bundles `bison`/`m4`/`python3` as host tools.
- `flex` must be built from source to `tools/hosttools/` (the one missing host
  tool; no sudo needed).
- `source tools/env.sh` exports `PATH`, `CROSS_COMPILE=aarch64-linux-`, `ARCH=arm64`.

## Kernel source

- Mainline: `git clone --depth 1 https://github.com/torvalds/linux.git src/linux`
- Vendor GPL reference (Microsoft, branch `surfaceduo2/11/2023.501.24`):
  `microsoft/surface-duo-oss-kernel.msm-5..4`, `surface-duo-oss-sm8350.11.display-devicetree`,
  `surface-duo-oss-sm8350.12.devicetree` — all on GitHub, not Azure DevOps.

## Config + build

```bash
source tools/env.sh
cd src/linux
make O=../build defconfig
scripts/config --file ../build/.config \
  -e DRM -e DRM_MSM -e DRM_PANEL_ELGIN -e HID_OVER_SPI -e BACKLIGHT_CLASS_DEVICE \
  -e QCOM_LLCC -d QCOM_OCMEM -e U_SERIAL_CONSOLE
make O=../build olddefconfig
make O=../build -j2 Image            # 53 MB Image, everything built-in
make O=../build qcom/sm8350-microsoft-surface-duo2.dtb
```

**Critical:** always pass `ARCH=arm64 CROSS_COMPILE=aarch64-linux-` explicitly
on every `make` line. A bare `make` defaults to `ARCH=x86` and silently rewrites
the config, disabling `ARCH_QCOM` (see [Gotchas](gotchas.md)).

## Boot image (header v3)

The Duo 2 uses boot header v3, where the **dtb lives in `vendor_boot`, not the
boot image**, and this kernel has no `ARM64_APPENDED_DTB`. So two images:

```bash
# minimal (empty) ramdisk
mkdir -p initramfs-root && (cd initramfs-root && find . | cpio -o -H newc) > ramdisk.cpio

# boot.img — kernel + ramdisk + cmdline
python3 tools/mkbootimg.py --kernel src/build/arch/arm64/boot/Image \
  --ramdisk ramdisk.cpio --header_version 3 --pagesize 4096 --base 0x0 \
  --cmdline "console=ttyGS0,115200n8 console=ttyMSM0,115200n8" \
  --os_version 11.0.0 --os_patch_level 2024-10 -o boot.img

# vendor_boot.img — mainline dtb + cmdline
python3 tools/mkbootimg.py --vendor_boot vendor_boot.img --vendor_ramdisk ramdisk.cpio \
  --dtb src/build/arch/arm64/boot/dts/qcom/sm8350-microsoft-surface-duo2.dtb \
  --vendor_cmdline "console=ttyGS0,115200n8 console=ttyMSM0,115200n8" \
  --header_version 3 --pagesize 4096 --base 0x0 \
  --os_version 11.0.0 --os_patch_level 2024-10
```

## Boot test (slot A, Android preserved on slot B)

```bash
fastboot set_active a
fastboot flash boot_a boot.img
fastboot flash vendor_boot_a vendor_boot.img
fastboot reboot
# return to Android: fastboot set_active b && fastboot reboot
```

Console is a **USB gadget serial** (`ttyGS0`) — readable on the host as a
CDC-ACM device, and it survives wedges (kernel-level, not userspace).
