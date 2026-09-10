# Surface Duo 2 display — elgin panel spec (AUTHORITATIVE, from vendor source)

Source: `surface-duo-oss-sm8350.11.display-devicetree` repo, branch
`surfaceduo2/11/2023.501.24`, files `surface_elgin_dsi0_c3_cmd_mp.dtsi`
(DSI0, DDIC "c3") and `surface_elgin_dsi1_r2_cmd_mp.dtsi` (DSI1, DDIC "r2"),
plus `surface_elgin_dsi_sync_cmd_mp.dtsi` (spanned/sync).
MP = mass production = what the device ships. This supersedes the earlier
reverse-engineered DTBO notes (which carried a 1440x2880 sw43404 *reference*
template — NOT the elgin).

## Resolution (RESOLVED)

- **1344 x 1892** per panel, both DSI controllers. (Android reports 2688x1892
  spanned = two 1344x1892 panels — matches.)
- physical dimension 85 x 120 mm.

## Panel identity

- name: "dsi elgin dsc dsi0 c3 cmd mp" / "dsi elgin dsc dsi1 r2 cmd mp"
- type: `dsi_cmd_mode`, physical `oled`, bpp 24, color-order "rgb_swap_rgb"
- DSI0 panel: DDIC rev "c3"; DSI1 panel: DDIC rev "r2" (identical geometry,
  near-identical init).
- 4 lanes, lane-map 0123, traffic-mode non_burst_sync_event.
- DDIC vendor: not yet pinned. (The "sw43404" label in the older DTBO is a
  reference panel; the elgin init uses Samsung-style UCS/MCS key sequences.
  Confirm DDIC against the main kernel's panel list before finalizing the
  mainline compatible string.)

## Timing (two modes)

| | 60 Hz (timing@0) | 90 Hz (timing@1) |
|---|---|---|
| framerate | 60 | 90 |
| h-front/back/pulse | 32 / 32 / 32 | 32 / 32 / 32 |
| v-back/front/pulse | 32 / 32 / 10 | 32 / 32 / 10 |
| panel clockrate | 700 MHz | 760 MHz |
| t-clk post / pre | 0x0B / 0x16 | 0x0B / 0x17 |

## DSC (per panel)

- version 0x11 (DSC 1.1), scr 0x0, encoders 1
- slice-per-pkt 2, slice-width 672, slice-height 946, bpc 8, bpp 8, block-prediction
- PPS (128-byte picture parameter set) embedded in the on-command `0A` write.

## Reset / power

- reset-sequence <0 10> <1 10> (assert/deassert 10 ms)
- surface-platform-freq-force-gpio = tlmm 166, surface-platform-freq-sel-gpio = tlmm 167

## Init sequence (from qcom,mdss-dsi-on-command — fully commented upstream)

Order (each `NN LL 00 00 XX YY 00 …` is a DCS/DSI cmd; LL=delay):
  1. 3B write 7F 5A 5A          UCS access
  2. 3B write F0 / F1 / F2 5A 5A  MCS access LV1/LV2/LV3
  3. 3B write E7 00              flash access
  4. 15 write 02 01              enable DSC
  5. 15 write 59 00              command DSC mode
  6. 39 write 2A 00 00 05 3F     column addr 0..1343
  7. 39 write 2B 00 00 07 63     page addr 0..1891
  8. 39 write 51 05 87           default DBV 350 nits
  9. 15 write 53 20              brightness control ON
  10. 15 write 55 04             PLC rate1
  11. 39 write 57 20 01 00 F8 FF F0 00   mLPIS
  12. 39 write 58 00 00 00 F8 FF FC      AoD mLPIS (all OFF)
  13. 15 write 76 00|01          DHFR mode (60/90 Hz)
  14. 0A write <128-byte PPS>    DSC picture parameter set
  15. 39 write 74 05 00 34       QSYNC_EN=1 EXT_TRIG=1 QSYNC_TO=208
  16. 15 write 35 00             TE on
  17. 39 write 44 00 00          TE_LINE=0
  18. 05 write 11 (wait 130 ms)  sleep out
  19. 39 write E5 00 20          force DCS TE
  20. VFP trimming: B0 08 + E1 00 04 4C ; B0 10 + E1 00 00 18
  21. 39 write B0 70, then EC 01

off-command: 28 (display off, wait 20 ms) then 10 (sleep in, wait 110 ms).
post-panel-on: 29 (display on).

## Surface-specific extras (Microsoft props)

- surface-ext-trig-enable = 1
- surface-dsi-elvss-levels = 10
- surface-dsi-aod-on/off-commands (AoD 8-color mode + DBV writes)
- surface-dsi-switch-rr-60/90-commands (DHFR 76 write)
- Backlight: DCS-based (53 20 + 51 DBV write).

## Driver-authoring notes

- Mainline template: `drivers/gpu/drm/panel/panel-boe-bf060y8m-aj0.c` (SW43404
  DDIC) for DSI command-mode + DSC + DCS backlight structure. Adapt: resolution
  1344x1892, the above init/PPS, DSC 1.1 (slice 672x946, per-pkt 2), dual
  timing 60/90 Hz.
- Two instances (DSI0 + DSI1): same driver, two DT nodes (c3 vs r2).
- The Surface extras (elvss/AoD/rr-switch/ext-trig) map to a custom
  brightness/refresh + AoD interface — source in the main kernel's surface
  panel glue (to be pulled from `surface-duo-oss-kernel.msm-5..4`).