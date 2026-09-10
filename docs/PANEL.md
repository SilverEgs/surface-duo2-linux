# Surface Duo 2 display — elgin panel engineering spec

Extracted from vendor DTBO (dtbo_b.img.dtb9.dts, model "Surface Duo2 MP", active
dtbo_idx=9) and Android dumpsys.

## Panel identity

- DDIC: Samsung **SW43404** (AMOLED DSI DDIC). Panel module manufactured by BOE.
- Mainline template: `drivers/gpu/drm/panel/panel-boe-bf060y8m-aj0.c`
  (.name = "panel-sw43404-boe-fhd-amoled") — same DDIC, adapt for this panel.
- Panel type: `dsi_cmd_mode`, physical type `oled`.
- DSI: 4 lanes, dsi-ctrl-num=0, dsi-phy-num=0 (DSI0). Second panel on DSI1.

## AMBIGUITY — resolution must be resolved against vendor source

DTBO lists THREE panel configs (all sw43404 BOE):
- cmd mode  1440x2880 (0x5a0 x 0xb40)  <- has full on-command
- video mode 1440x2880
- fhd+ 1080x2160 (0x438 x 0x870)

But the live Android device reports 2688x1892 SPANNED (two panels) => 1344x1892
per panel @ 60/90 Hz, 401 dpi. NONE of the DTBO entries is 1344x1892.

Resolution + exact init sequence for the real "elgin" Duo 2 panel MUST be taken
from the vendor GPL kernel (microsoft/surface-duo-oss lahaina tree), NOT
assumed from the shared sw43404 template. The DTBO bytes below are the best
available reference but may be the reference-panel variant, not the Duo 2's.

## Timing (cmd-mode entry, timing@0)

- panel-width 0x5a0=1440, panel-height 0xb40=2880, framerate 0x3c=60Hz
- h-front-porch 60, h-back-porch 30, h-pulse-width 12, h-sync-skew 0
- v-front-porch 8, v-back-porch 8, v-pulse-width 1
- bpp 0x18=24, h-sync-pulse 0 (event mode)
- trafofic mode: non_burst_sync_event; dma-trigger trigger_sw; mdp-trigger none

## DSC (VESA DSC, command mode)

- compression-mode dsc, slice-width 0x2d0=720, slice-height 0xb4=180
- slice-per-pkt 1 (cmd) / 2 (video), bpc 8, bpp 8, block-prediction enabled
- roi-align 720x180; partial-update single_roi

## Power / reset

- platform-reset-gpio = tlmm 24 (0x18)
- reset-sequence: assert 10ms, deassert 10ms, assert 10ms
- panel-supply-entries (regulator list; cross-ref dtb9 display_gpio_regulator,
  display_panel_avdd @ fragment@47)

## Backlight (DCS)

- bl-pmic-control-type bl_ctrl_dcs; bl-min-level 1; bl-max-level 0x3ff=1023
- brightness-max-level 0xff=255

## HDR

- enabled; peak-brightness 0x401640; blackness 0xc9e; color primaries captured
  in the dtb9 source.

## Init / state commands (raw qcom DSI encodings — transcribe to mipi_dsi)

- on-command  (line 85 of dtb9) — full SW43404 init (reg writes, gamma, DSC on)
- off-command (line 86) — 0x28 display off, 0x10 sleep in
- lp1-command  0x39 (enter idle/DSI lp) — low-power mode
- nolp-command 0x38 (exit idle)
- status-check: reg_read 0x0a, expect value 0x9c; esd-check enabled

## Notes for driver authoring

- mainline `panel-boe-bf060y8m-aj0.c` is the structural template (SW43404:
  prepare/unprepare, enable/disable, DSC, DCS backlight). Swap in this panel's
  resolution/timing/init from the vendor source.
- Two instances (DSI0 + DSI1) — instantiate the same panel driver on both
  controllers; posture/hinge logic decides single vs dual vs spanned output.
- Confirm resolution + init against vendor source before writing the driver.