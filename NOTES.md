# Surface Duo 2 (`zeta`) — Bring-up Notes

Device: Surface Duo 2 (Qualcomm SM8350 "Lahaina"), serial 0F00Q6U213800A
Stock build: duo-de A15 GSI on slot B, kernel 5.4.233-qgki, bootloader 2023.501.53.
Bootloader: UNLOCKED. Active slot: b (current-slot:b, slot-successful:b:yes).

## Phase 0 artifacts (hashed, quark-safe)

Full partition backups live in `artifacts/private/<serial>/` (gitignored, DO NOT COMMIT):
- Critical/irreplaceable: modemst1, modemst2 (IMEI/radio NV), persist (sensor trim),
  fsg, fsc, secdata, spunvm, apdp, devinfo, misc, frp, ssd.
- Boot ground truth: boot_a/b, vendor_boot_a/b, dtbo_a/b, vbmeta_a/b.
- `MANIFEST.txt` = sha256 + byte-size per image.
- Restore slot B: `fastboot flash boot_b boot_b.img` + `vendor_boot_b`, `dtbo_b`.

Device tree ground truth in `artifacts/reference/` (pure-python FDT dumper, no dtc):
- `vendor_boot_*.img.dtb{0,1,2}.dts` = base SM8350 SoC tree (model "Lahaina V2.1 SoC"),
  ~485 KB each; MDSS @ ae00000, DSI0 @ ae94000, DSI1 @ ae96000.
- `dtbo_{a,b}.img.dtb{0..11}.dts` = 12 board overlays per slot. ACTIVE overlay =
  index 9 (`ro.boot.dtbo_idx=9`), `model = "Surface Duo2 MP"`.

## Hardware map (from active dtbo dtb9)

- Display: TWO panels, codename "elgin", one per DSI controller (DSI0 + DSI1),
  qcom,dsi-display, COMMAND mode + DSC. `qcom,mdss-dsi-panel-name = "dsi elgin
  dsc dsi0 c3 cmd*" / "dsi elgin dsc dsi1 r2 cmd*"`. Panel clock ~700–900 MHz.
  Android logical display: 2688x1892 @90Hz (renders both panels as one).
- Touch: `hid-over-spi` (compatible "hid-over-spi").
- Hinge/posture: hinge_angle sensor + hall effect + `ams,tof8801` (ToF),
  `surface,ponhob-smem`, `surface,oem-smem` SMEM drivers.
- Biometrics: `fpc,fpc1020` fingerprint.
- SoC USB: `ro.boot.usbcontroller = a600000.dwc3`; UFS: `1d84000.ufshc`.

## Bring-up gotchas

- USB re-enumerates across modes and VID/PID does NOT stay 045e:0c26:
  - Android/ADB + MTP = 045e:0c26
  - fastboot = 045e:0c2f
  - TWRP = 18d1:d001 (lsusb mislabels "Nexus 4")
  libvirt hostdev is by vendor:product, so EACH mode needs its own hostdev
  attached (and --config for persistence). See tools/usb-modes.txt.
- VM (vm-hermes) runs on thinkserver via libvirt/qemu. virsh needs
  `-c qemu:///system` (constable is in libvirt group but non-interactive SSH
  has no LIBVIRT_DEFAULT_URI).
- TWRP: use WOA surfaceduo2-twrp.img via `fastboot boot` (RAM-only, writes
  nothing). Touch dead + screen "locked" = normal. adb runs as root (uid=0),
  `setenforce 0` to permissive before dd.
- boot partition has NO dtb (0 blobs); dtb lives in vendor_boot (base) +
  dtbo (overlays). boot header version = 3, page size 4096.

## Build status (vm-hermes) — WORKING

- Cross toolchain: Bootlin `aarch64--glibc--stable-2024.02-1` (gcc 12.3.0) at
  `tools/toolchains/`; it ALSO bundles host tools `bison`/`m4`/`python3`.
- `flex` was the one missing host tool (needed by Kconfig); built from source to
  `tools/hosttools/` (no sudo). `source tools/env.sh` sets PATH + `CROSS_COMPILE`
  + `ARCH=arm64`.
- Build recipe (out-of-tree): `make O=../build defconfig` →
  `scripts/config --file ../build/.config --module DRM_PANEL_ELGIN --module HID_OVER_SPI`
  → `make O=../build olddefconfig prepare`.
  **Both drivers compile clean (zero warnings)** against this tree.
- **CONFIG GOTCHA (cost 3 rebuilds):** running `make Image` with `ARCH=x86`
  (host default) silently re-syncs the O= config for x86 and DISABLES
  `CONFIG_ARCH_QCOM`, which cascades to `DRM_MSM=n` (its `depends on
  ARCH_QCOM` fails) and kills the whole Qualcomm build. ALWAYS pass
  `ARCH=arm64 CROSS_COMPILE=aarch64-linux-` explicitly on every make line.
  Also: `DRM_MSM=y` needs its `QCOM_*` deps at =y or =n (not =m) — the
  `X || X=n` pattern. In the arm64 defconfig, `QCOM_LLCC=m` and `QCOM_OCMEM=m`
  block it; fix with `-e QCOM_LLCC -d QCOM_OCMEM`. Final working config:
  `ARCH_QCOM=y DRM=y DRM_MSM=y DRM_PANEL_ELGIN=y HID_OVER_SPI=y
  BACKLIGHT_CLASS_DEVICE=y` → 53 MB Image with msm_drm/dpu/elgin/hidspi
  all built-in (verified via System.map).
- `dtc` still not installed (no sudo) — use `tools/fdt2dts.py` for DTB→DTS.
- Mainline kernel: shallow clone at `src/linux` (torvalds HEAD). Vendor source:
  GitHub `microsoft/surface-duo-oss-*` (NOT Azure Devops), branch
  `surfaceduo2/11/2023.501.24`, sparse-blobless-cloned under `src/vendor/`.

## Phase 1 — upstream gap analysis (KEY FINDING)

Mainline ALREADY has a dedicated board file for this device:
  arch/arm64/boot/dts/qcom/sm8350-microsoft-surface-duo2.dts
  (copyright Microsoft 2021; compatible "microsoft,surface-duo2","qcom,sm8350").
So this is NOT a cold start.

Present upstream (works out of the box on this SoC / board):
- sm8350.dtsi: full SoC — CPU, pinctrl (tlmm), qupv3 (I2C/SPI/UART), UFS,
  USB dual-role + PHYs, PMIC RPMH, remoteprocs (adsp/cdsp/mpss/slpi),
  MDSS @ ae00000 (qcom,sm8350-mdss), DPU @ ae01000 (qcom,sm8350-dpu),
  DSI0 @ ae94000 + DSI1 @ ae96000 (qcom,sm8350-dsi-ctrl) + PHYs, DisplayPort,
  Adreno 660 (qcom,adreno-660.1) + GMU + SMMU.
- Board file enables UFS, USB (peripheral), UART2 console (115200n8),
  remote procs + firmware paths, I2C10/11, qupv3, tlmm reserved ranges, regulators.
- Board list also has sm8350-mtp, sm8350-hdk, sm8350-sony-xperia-sagami-* (same SoC).

MISSING (the actual port work — all dual-screen-specific, all unpublished):
1. Display panels — codename "elgin", DSI0+DSI1, CMD mode + DSC. No DRM panel
   driver in mainline; no wiring of &mdss/&mdss_mdp/&mdss_dsi0/1 in the board
   dts. MUST AUTHOR: a DRM DSI panel driver (init sequence from vendor GPL
   source DDIC) + board-dts display/regulator/backlight wiring. Reference =
   extracted dtbo dtb9 ("Surface Duo2 MP") + vendor kernel.
2. Touch — Microsoft "d6" HID-over-SPI controller, node d6_hid@0 on
   qupv3_se4_spi (CS0, 32 MHz, IRQ gpio84, hid-descr-addr=1). No mainline
   SPI device driver matches "hid-over-spi" (only include/linux/hid-over-spi.h
   + Intel THC quickspi driver exist). MUST AUTHOR an HID-over-SPI SPI device
   driver (d6) following the HID-over-SPI spec.
3. Hinge/posture — hinge angle + hall + ams,tof8801 ToF + Surface SMEM. No
   drivers. Needed for posture->workspace tiling; lower priority than display.
4. (defer) Wi-Fi/BT, audio, camera, modem.

Community: no postmarketOS device page, no published elgin/d6 driver work —
thin results (normal case). Vendor GPL source = microsoft/surface-duo-oss
(lahaina tree, repo/Azure-DevOps manifest, msm-4.14 + gki variants).

Priority for a booting Linux: the board file already gives console+UFS+USB
(silent bring-up target is a USB ACM/serial console — reachable without ANY
display/touch work). Display (elgin panels) is the first authored milestone
and the hard part; touch (d6) is second.

---

## Boot bring-up status (stock QGKI kernel + initramfs) — IN PROGRESS

CONFIRMED: the extracted stock kernel (5.4 QGKI, `boot_a`'s prebuilt `kernel`)
boots a custom static-busybox initramfs and runs `/init` (evidenced by an
intentional boot loop from the diagnostic init). This is the fastest round-trip
to booting Linux on the device — no display/touch drivers needed yet.

THE BLOCKER is a console/visibility channel, not the boot itself:
- No UART (case stays closed).
- No screen console: QGKI kernel has `CONFIG_VT` and `DRM_FBDEV_EMULATION` unset,
  so `console=tty0` is impossible; the "static Windows logo" is just the
  bootloader's frozen frame.
- USB gadget: only `USB_CONFIGFS_ACM`+`NCM` are `=y` (no RNDIS/ECM, no
  `U_SERIAL_CONSOLE`) — so the target is a configfs ACM serial gadget. Tried:
  `dr_mode=peripheral` + removing the `extcon` property (`qcom,msm-eud` phandle
  that makes `dwc3-qcom` defer forever). dtb patches are valid and packed, but
  the device still drops off USB (no enumerate).
- pstore/pmsg: `/dev/pmsg0` write succeeds but the region does NOT survive the
  reboot (ramoops `mem_type=0` in recovery; region returned empty). Not a
  reliable console path yet.

REMAINING HYPOTHESES (all need console visibility to disambiguate): the Type-C
CC-pin/role configuration, the USB QMP PHY not powering for device mode, or
another `dwc3-qcom` probe dependency.

NEXT: mirror the known-working USB setup from the postmarketOS Duo 2 port
(their wiki lists "USB Networking: Works").