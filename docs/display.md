# Display — elgin panel driver

Full engineering spec: see [`PANEL.md`](PANEL.md) (timing, DSC, init bytes).

## Driver

`drivers/gpu/drm/panel/panel-elgin.c` — mainline DRM MIPI-DSI panel driver.

- Compatible: `microsoft,elgin-c3` / `microsoft,elgin-r2` + fallback `microsoft,elgin`.
- 1344×1892 command mode, 4 lanes, DSC 1.1 (packs the PPS from config via
  `drm_dsc_pps_payload_pack`), DCS backlight (11-bit DBV).
- Init sequence transcribed from the vendor `surface_elgin_dsi0_c3_cmd_mp.dtsi`.
- Template: mainline `panel-boe-bf060y8m-aj0.c` (same SW43404 DDIC).

## Status

- ✅ authored, compiles clean, linked into the kernel.
- ⏳ **not yet proven on hardware.** Bring-up unknowns, flagged in the driver:
  1. Reset polarity (`GPIO_ACTIVE_LOW` — derived from the vendor reset sequence).
  2. DDIC vendor not 100% confirmed (compatible uses the module name "elgin").
  3. 90 Hz DHFR switch + 760 MHz clock not wired (60 Hz only for now).

## Board wiring

`arch/arm64/boot/dts/qcom/sm8350-microsoft-surface-duo2.dts` — enables
`mdss`/`dsi0`/`dsi1` + PHYs, two panel nodes, regulator mapping
`vdd → vreg_l7c_3p0`, `vddio → vreg_l1c_1p8` (rail-voltage-derived; the Duo 2
board-level DTS is absent from the GPL release).
