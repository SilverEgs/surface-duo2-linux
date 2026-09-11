# Surface Duo 2 — Mainline Linux Bring-up

Porting mainline Linux to the Microsoft Surface Duo 2 (dual-screen foldable,
Qualcomm SM8350 "lahaina") as a base for a Hyprland-on-Wayland mobile desktop.

This wiki tracks the reverse-engineering, driver-authoring, and bring-up work.
Every page is the shareable record of what was found, built, and learned.

> **Assisted project** — research, orchestration, and verification by
> [Hermes](https://hermes-agent.nousresearch.com) (Nous Research); driver
> authoring and code review by [Claude Code](https://claude.ai/code) (Anthropic).

## Status at a glance

| Milestone | State |
|---|---|
| Phase 0 — partition backup + device-tree ground truth | ✅ done |
| Phase 1 — gap analysis (what mainline already has) | ✅ done |
| **elgin** dual-panel DRM driver | ✅ authored + compiles |
| **d6** touch (HID-over-SPI) driver | ✅ authored + compiles |
| Board device-tree wiring | ✅ done + `.dtb` compiles |
| Kernel build (SoC + display + touch built-in) | ✅ 53 MB `Image` |
| Boot image packing | ✅ `boot.img` + `vendor_boot.img` |
| Boot test on hardware | ⏳ next |
| Display / touch bring-up on silicon | ⏳ the hard part |
| Hinge / posture driver | ⏳ not started |
| Userspace (Arch/pmOS + Hyprland) | ⏳ far goal |

## Pages

- [Progress](progress.md) — chronological milestones and what each produced
- [Hardware](hardware.md) — the reverse-engineered hardware map
- [Display](display.md) — the elgin panel driver + spec
- [Touch](touch.md) — the d6 HID-over-SPI driver
- [Build](build.md) — reproducible toolchain, config, and boot-image guide
- [Gotchas](gotchas.md) — the lessons that cost real time

## Device

- SoC: Qualcomm **SM8350** ("Lahaina" / Snapdragon 888)
- Displays: 2× OLED "elgin" panels, 1344×1892, DSC 1.1, command mode
- Touch: Microsoft **"d6"** controller, HID-over-SPI
- Bootloader: UEFI, A/B slots, **unlocked**; boot image header v3
- Stock OS: `duo-de` Android 15 GSI on **slot B** (preserved; Linux targets slot A)

## License

GPL-2.0 — see the repository root `LICENSE`.
