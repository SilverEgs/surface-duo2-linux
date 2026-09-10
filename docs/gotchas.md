# Gotchas

Lessons that cost real time. Each one is a non-obvious failure mode.

## 1. `make` defaults to `ARCH=x86` — and silently destroys the config

Running `make Image` without `ARCH=arm64` re-syncs the O= config for x86 and
**disables `CONFIG_ARCH_QCOM`**, which cascades to `DRM_MSM=n` (its `depends on
ARCH_QCOM` fails) and kills the whole Qualcomm build. The `.dtb` may still
compile, so nothing warns you. Cost: 3 rebuilds.

**Fix:** always pass `ARCH=arm64 CROSS_COMPILE=aarch64-linux-` on every `make`.

## 2. `DRM_MSM=y` needs its QCOM deps at `=y` or `=n`

`DRM_MSM` has `depends on QCOM_LLCC || QCOM_LLCC=n` (and the same for
`QCOM_OCMEM`, `QCOM_AOSS_QMP`, `QCOM_COMMAND_DB`, `QCOM_SMEM`). If any is `=m`,
`olddefconfig` silently drops `DRM_MSM` back to `=n`. In the arm64 defconfig,
`QCOM_LLCC=m` and `QCOM_OCMEM=m` are the culprits.

**Fix:** `-e QCOM_LLCC -d QCOM_OCMEM` (LLCC is needed by sm8350; OCMEM is an
old-SoC driver).

## 3. VM USB passthrough is per-VID:PID, and the phone re-enumerates

libvirt passes USB by vendor:product, and the Duo 2 changes ID on every mode
switch (ADB `045e:0c26`, fastboot `045e:0c2f`, TWRP `18d1:d001`). A single
hostdev covers only one mode — the device drops from the guest exactly when
you issue `adb reboot bootloader`.

**Fix:** one hostdev per mode, persisted with `--config` *and* attached live.

## 4. No `ARM64_APPENDED_DTB` in modern kernels

This kernel dropped `CONFIG_ARM64_APPENDED_DTB`. With boot header v3, the dtb
must go in `vendor_boot` — not appended to the kernel and not in the boot image.

## 5. Vendor touch driver is proprietary

The Duo 2 "d6" touch driver isn't in Microsoft's GPL dump (it's a closed `.ko`).
But it speaks the public HID-over-SPI spec, so a generic transport driver +
the kernel HID layer is enough — no reverse-engineering of a private protocol.

## 6. Building a single `.ko` fails modpost

`make drivers/gpu/drm/panel/panel-elgin.ko` alone reports its DRM-core symbols
"undefined" because the sibling modules (`drm.ko`, `drm_dsc_helper`) weren't
built in that invocation. Not a driver bug — build all modules, or build the
drivers **built-in** (`=y`) instead.

## 7. `pkill -f` self-matches

`pkill -f 'aarch64-linux-gcc'` matches its own command line (the pattern is in
the shell's argv) and SIGTERMs itself. Filter the matcher out or use a PID.

## 8. Tailscale SSH needs per-session approval

`thinkserver` uses Tailscale SSH; every `ssh constable@…` prints an approval URL
the owner must click before the connection completes. No cached key exists.
