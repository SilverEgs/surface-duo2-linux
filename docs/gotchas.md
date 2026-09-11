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

## 9. The USB console (`ttyGS0`) won't enumerate from `defconfig`

`make defconfig` ships the USB gadget console as **modules** and omits the QMP
PHY drivers, so on an initramfs boot (no module loader) `ttyGS0` never comes up
even on a healthy kernel. Required, all built-in (`=y`):

- `USB_CONFIGFS`, `USB_LIBCOMPOSITE`, `USB_F_SERIAL`, `USB_U_SERIAL` (gadget stack)
- `PHY_QCOM_QMP`, `PHY_QCOM_QMP_USB`, `PHY_QCOM_QMP_COMBO` (USB3 PHYs), `PHY_QCOM_QUSB2` (USB2)
- `USB_DWC3`, `USB_DWC3_DUAL_ROLE`, `U_SERIAL_CONSOLE`

`PHY_QCOM_QMP_COMBO` is gated by `depends on TYPEC || TYPEC=n`; `defconfig` sets
`TYPEC=m`, which silently forces it back to `=m`. With `dr_mode="peripheral"`
(no role-switch/PD needed) set `TYPEC=n` to unblock it. Signature check that it
stuck: `grep '^CONFIG_…=' build/.config`, and `qmp_combo_probe`/`gserial` present
in `System.map`.

## 10. `source tools/env.sh` shadows the system Python

`env.sh` puts the Bootlin cross-toolchain's `bin` first on `PATH`, and that
toolchain's `python3` has **no `_ssl` module** — so any later `python3` script
doing HTTPS dies with `ModuleNotFoundError: No module named '_ssl'` (or a
misleading `urlopen error unknown url type: https`). Use `/usr/bin/python3`
explicitly for non-kernel Python work after sourcing `env.sh`.

## 11. Symbol names: don't guess Kconfig ids

Repeatedly wrong guessed ids cost time (`CONFIG_QCOM_GCC_SM8350` → actually
`CONFIG_SM_GCC_8350`; `CONFIG_ARM_PSCI` → `CONFIG_ARM_PSCI_FW`; `QCOM_QMP_PHY` →
`PHY_QCOM_QMP`). When checking a driver's config, grep the **Makefile**
(`obj-$(CONFIG_…) += driver.o`) for the real symbol, not the Kconfig by
guessed name.

## 12. Surface Duo 2 is UEFI + standard Android boot-image, not ABL

The Duo 2 uses Microsoft UEFI, but boots Android the *standard* way: header-v3
`boot.img` (kernel + ramdisk) + `vendor_boot.img` (dtb + cmdline), no
`ARM64_APPENDED_DTB` in this kernel. `fastboot set_active a` + `reboot` lands on
the UEFI boot menu (`Start / Recover / Power off`), which presents as `045e:0c2f`
on USB just like raw fastboot — the menu == "boot failed or needs manual
selection", not a different device state.

## 13. Reading pstore back needs a matching layout + a memory path

Reading a crash's pstore from a *different* boot only works if the region
survives the boot chain **and** the reading kernel can reach it. Two traps:
(a) a UEFI device may re-initialise an *arbitrary* RAM address you picked — use
the **vendor's own ramoops/rstdump address** instead (it's guaranteed to survive;
on the Duo 2 it's `0xA9000000`, 2 MiB), and (b) the recovery kernel usually ships
`STRICT_DEVMEM` (no `/dev/mem`), so `devmem`/`dd` can't read raw RAM — the reading
kernel must mount pstore itself via a DT node **matching your layout** (same
address *and* same `record-size`/`console-size`/`pmsg-size`), or the records
mis-parse.
