# Hardware map

Reverse-engineered from the stock device-tree (extracted from the device) and
the vendor GPL source. Authoritative unless noted.

## SoC

- Qualcomm **SM8350** ("Lahaina" / Snapdragon 888), A/B slots.
- Stock kernel: `5.4.233-qgki`, bootloader `2023.501.53`.
- USB: `a600000.dwc3`; UFS: `1d84000.ufshc`; console UART2 = `ttyMSM0`.

## Displays — "elgin" panels

- Two OLED panels, one per DSI controller (DSI0 + DSI1).
- Resolution **1344×1892** each (Android renders 2688×1892 spanned).
- Command mode + **DSC 1.1** (slice 672×946, 2 slices/pkt, 8 bpc / 8 bpp).
- 60 Hz / 90 Hz (DHFR switch via DDIC register `0x76`).
- DDIC: Samsung **SW43404** family (BOE-made module). "c3" stepping on DSI0,
  "r2" on DSI1.
- Backlight: DCS (`0x51` DBV write), 11-bit.
- Reset: tlmm 24 (shared — unverified for DSI1).

## Touch — "d6" HID-over-SPI

- Microsoft "d6" controller on `qupv3_se4` SPI (CS0, 32 MHz, IRQ gpio 84).
- Protocol: **HID-over-SPI** (HIDSPI), `hid-descr-addr = 1`.
- The vendor driver is proprietary; mainline has the protocol framework only.

## Hinge / posture

- Hinge angle sensor + hall effect + `ams,tof8801` ToF.
- Surface SMEM drivers (`surface,oem-smem`, `surface,ponhob-smem`).

## Input / sensors (from Android)

- `surface_touchscreen`, `surface_tail_button`, `qcom-hv-haptics`, `qpnp_pon`.
- Two accelerometer/gyro/magnetometer/light/proximity sensors (one per half).
- Hinge angle + hall + posture (via `com.thain.duo.PostureProcessorService`).

## USB re-enumeration table

The phone changes VID:PID on every mode switch (matters for VM USB passthrough):

| Mode | VID:PID |
|---|---|
| Android / ADB / MTP | `045e:0c26` |
| fastboot | `045e:0c2f` |
| TWRP (headless) | `18d1:d001` |
