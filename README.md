# Surface Duo 2 — Mainline Linux bring-up

Porting mainline Linux to the Microsoft Surface Duo 2 (a dual-screen, foldable
Android phone) as a base for a Hyprland-on-Wayland mobile desktop. This tracks
the reverse-engineering, driver-authoring, and bring-up effort for the
dual-screen-specific hardware that upstream Linux does not yet support.

> Status: **Phase 0 (backup + device-tree ground truth) and Phase 1 (gap
> analysis + toolchain) are complete. Driver authoring is in progress.**

## Device

| | |
|---|---|
| SoC | Qualcomm **SM8350** ("Lahaina" / Snapdragon 888) |
| Kernel | stock 5.4.233-qgki, bootloader 2023.501.53, A/B slots |
| Displays | 2× OLED, DDIC **Samsung SW43404** (BOE panel), DSI0+DSI1, CMD+DSC |
| Touch | Microsoft **"d6"** controller, HID-over-SPI, `qupv3_se4` |
| Hinge | hinge-angle + hall + `ams,tof8801` ToF + Surface SMEM |
| Current OS | `duo-de` Android 15 GSI on slot B (left intact; Linux targets slot A) |

## What already works upstream (why this is not a cold start)

Mainline ships a dedicated board file, courtesy of Microsoft (2021):

```
arch/arm64/boot/dts/qcom/sm8350-microsoft-surface-duo2.dts
```

…plus the full SM8350 SoC tree (`sm8350.dtsi`: UFS, USB, UART2 console, PMIC
RPMH regulators, remoteprocs, MDSS/DPU/DSI controllers + PHYs, Adreno 660 GPU).
So a mainline kernel already boots this board to a serial/USB console with
storage and USB working.

## The gap this project fills

Three dual-screen-specific pieces have **no mainline driver anywhere** (verified:
no postmarketOS port, no community effort):

| Component | Driver to author | Status |
|---|---|---|
| **"elgin" panels** (encoded as `sw43404` in vendor DTBO) | DRM DSI panel driver + board-dts display wiring | in progress |
| **"d6" touch** (HID-over-SPI) | HID-over-SPI SPI device driver | queued |
| **hinge / posture** | hinge-angle + hall + ToF input | low priority |

## Repository layout

```
README.md        this file
NOTES.md         working findings (hardware map, gotchas, toolchain)
docs/PANEL.md    elgin/sw43404 panel engineering spec (timing, DSC, init)
docs/            per-component specs as they are extracted
tools/           helpers: fdt2dts.py (pure-python DTB->DTS), carve-dtb.py, etc.
AGENTS.md        bring-up protocol (never flash without backup etc.)
```

Not committed (see `.gitignore`): `artifacts/` (device partition backups —
**including irreplaceable device-unique calibration data that must never be
published**), `src/` (kernel source trees), `tools/toolchains/` (cross compiler).

## Reproduce

1. **Toolchain** — Bootlin `aarch64--glibc--stable-2024.02-1`, or any
   `aarch64-linux-` gcc. From `tools/`, `source env.sh` exports
   `CROSS_COMPILE`/`ARCH=arm64`.
2. **Kernel** — `git clone --depth 1 https://github.com/torvalds/linux.git src/linux`.
3. **Vendor reference** — Microsoft `surface-duo-oss` GPL kernel tree
   (branch `surfaceduo2/11/<build>`, matching `/proc/version`) for the original
   "elgin" panel + "d6" touch drivers.
4. **Device ground truth** — stock `vendor_boot` + `dtbo` DTBs (extracted via
   TWRP) decompiled to `.dts` with `tools/fdt2dts.py`.

## Safety

- Full partition backup (including `modemst1/2`, `persist` — IMEI/radio/sensor
  calibration) is taken and verified before any flash, and stays off-repo.
- One variable per flash; every change verified by reading the partition back.
- Slot A/B dual-boot keeps the stock Android recoverable at any time.

## License

GPL-2.0 — see `LICENSE`.