# Progress

Chronological record of the port. Each entry is what was done and what it produced.

## Phase 0 — backup + device-tree ground truth

- Dumped **20 partitions** off the device via headless TWRP (`fastboot boot`
  into RAM, nothing flashed): `modemst1/2`, `persist`, `fsg`, `fsc`, `secdata`,
  `spunvm`, `apdp`, `devinfo`, `misc`, `frp`, `ssd`, and the boot ground truth
  `boot_a/b`, `vendor_boot_a/b`, `dtbo_a/b`, `vbmeta_a/b`.
- Every image SHA-256 hashed into a `MANIFEST.txt`. Device-unique partitions
  (IMEI/radio/sensor calibration) are private — **never published**.
- Decompiled the stock device-tree: base SM8350 tree (`vendor_boot`) + 12 DTBO
  board overlays per slot. The active overlay is `dtbo_idx=9` = "Surface Duo2 MP".
- Key hardware identified: "elgin" panels (DSI0+DSI1), "d6" HID-over-SPI touch,
  hinge angle + hall + ToF.

## Phase 1 — gap analysis

- Mainline **already ships** `arch/arm64/boot/dts/qcom/sm8350-microsoft-surface-duo2.dts`
  (Microsoft, 2021) plus the full SM8350 SoC tree (UFS, USB, MDSS/DPU/DSI, Adreno 660).
- So this is **not a cold start**: a mainline kernel already boots this board to
  a serial/USB console.
- The gap is three dual-screen-specific pieces with **no driver anywhere**:
  the elgin panels, the d6 touch, and the hinge.

## Drivers authored

### elgin panel (`drivers/gpu/drm/panel/panel-elgin.c`)
- 1344×1892 command-mode OLED, DSC 1.1 (slice 672×946, 8 bpp), dual 60/90 Hz.
- Init sequence transcribed byte-for-byte from the vendor
  `surface_elgin_dsi0_c3_cmd_mp.dtsi` (unlock keys → DSC enable → addresses →
  DBV → PPS → TE → sleep-out → VFP trim).
- Structural template: mainline `panel-boe-bf060y8m-aj0.c` (same SW43404 DDIC).
- DT binding: `Documentation/devicetree/bindings/display/panel/elgin.yaml`.
- Compiles clean (zero warnings) against the real kernel tree.

### d6 touch (`drivers/hid/hid-over-spi.c`)
- The vendor "d6" touch driver is **proprietary** (not in the GPL dump), so this
  is authored from the public HID-over-SPI (HIDSPI) spec.
- Mainline ships the protocol framework (`include/linux/hid-over-spi.h`); the
  driver binds a `spi_device` and feeds reports to the HID subsystem.
- DT binding: `Documentation/devicetree/bindings/input/hid-over-spi.yaml`.
- Compiles clean (one `FIELD_GET` cast fixed).

## Board device-tree wiring

- Enabled `mdss`/`dsi0`/`dsi1` + PHYs, added both panel nodes (DSI0 `c3`, DSI1
  `r2`) with vdd/vddio/reset/ports, and the touch node on `spi4` (`qupv3_se4`).
- `.dtb` compiles clean; all three nodes verified in the decompiled binary.

## Kernel build

- Cross-toolchain: Bootlin `aarch64--glibc--stable-2024.02-1` (gcc 12.3.0).
- Config: `ARCH_QCOM=y DRM=y DRM_MSM=y DRM_PANEL_ELGIN=y HID_OVER_SPI=y BACKLIGHT_CLASS_DEVICE=y`.
- 53 MB `Image` with `msm_drm`/`dpu_`/`elgin_*`/`hidspi_*` all linked in (verified via `System.map`).

## Boot image

- `boot.img` (header v3, kernel + empty ramdisk, `console=ttyGS0` USB console).
- `vendor_boot.img` (header v3, mainline dtb + cmdline).
- Next: `fastboot flash vendor_boot_a` + `fastboot boot` on slot A.
