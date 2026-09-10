# Touch — d6 HID-over-SPI driver

## Driver

`drivers/hid/hid-over-spi.c` — generic HIDSPI transport driver.

- Compatible: `hid-over-spi`.
- Binds a Linux `struct spi_device`, reads the 16-byte HID descriptor at
  `hid-descr-addr`, registers with the HID subsystem (`hid_allocate_device` +
  `hid_add_device`), and forwards input reports via `hid_input_report`.
- Uses the mainline protocol framework `include/linux/hid-over-spi.h`
  (input/output report types, 0x5A-sync header, body-header structs).
- Structural references: `intel-thc-hid/intel-quickspi` (protocol) and
  `i2c-hid-core.c` (HID subsystem integration).

## Key finding

The vendor "d6" touch driver is **proprietary** (a closed `.ko`, absent from
Microsoft's GPL kernel dump). Because it speaks the public HID-over-SPI spec,
a generic transport driver + the kernel HID layer is enough — no d6-specific
protocol needed.

## Status

- ✅ authored, compiles clean, linked into the kernel (`hidspi_probe` in System.map).
- ⏳ **not proven on hardware.** Unverified: IRQ trigger (edge-falling, from the
  binding example), the optional `reset-gpios` (tlmm 85), and `spi4` pinctrl
  (no `pinctrl-0` in `sm8350.dtsi`).
