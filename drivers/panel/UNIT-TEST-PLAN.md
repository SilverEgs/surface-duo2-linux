# panel-elgin KUnit test plan

Scope: `drivers/gpu/drm/panel/panel-elgin.c`. No build/test harness is wired up
yet, so this documents what to test, which functions to point the tests at, and
what fixtures they need. Everything here is host-independent — no Surface Duo 2
hardware and no DPU/DSI hardware is required.

Target file: `drivers/gpu/drm/panel/panel_elgin_test.c`, `CONFIG_DRM_PANEL_ELGIN_KUNIT_TEST`
(`depends on DRM_PANEL_ELGIN && KUNIT` — the module cannot be built as a
separate `.ko` because it needs the driver's static symbols; see "Visibility").

## Visibility

Both units under test are `static`. Two ways in, pick one when the harness
lands:

1. `#include "panel_elgin_test.c"` at the bottom of `panel-elgin.c` under
   `#if IS_ENABLED(CONFIG_DRM_PANEL_ELGIN_KUNIT_TEST)` — the pattern used by
   several in-tree drivers, keeps everything static.
2. Mark `elgin_dsc_init()` and `elgin_on()` with `VISIBLE_IF_KUNIT` plus
   `EXPORT_SYMBOL_IF_KUNIT()` from `<kunit/visibility.h>` and build the test as
   its own module.

Option 1 is preferred here: neither function is useful outside the driver, and
`elgin_on()` needs `struct elgin`, which is also private.

## Suite 1 — DSC configuration

### 1.1 `elgin_dsc_init()` sets the documented panel constants

Unit: `elgin_dsc_init(struct drm_dsc_config *dsc)`.

No fixture. Call it on a zeroed `struct drm_dsc_config` and assert every field
against the vendor device tree
(`surface_elgin_dsi0_c3_cmd_mp.dtsi`, `qcom,mdss-dsc-*`):

| field | expected | vendor property |
| --- | --- | --- |
| `dsc_version_major` / `minor` | 1 / 1 | `qcom,mdss-dsc-version = <0x11>` |
| `slice_width` | 672 | `qcom,mdss-dsc-slice-width` |
| `slice_height` | 946 | `qcom,mdss-dsc-slice-height` |
| `slice_count` | 2 | `qcom,mdss-dsc-slice-per-pkt` |
| `bits_per_component` | 8 | `qcom,mdss-dsc-bit-per-component` |
| `bits_per_pixel` | 128 (8 << 4) | `qcom,mdss-dsc-bit-per-pixel` |
| `block_pred_enable` | true | `qcom,mdss-dsc-block-prediction-enable` |

Also assert the geometry invariants the msm DSI host enforces at
`dsi_populate_dsc_params()`/`dsi_dsc_compression_enable()` time, because getting
them wrong is the most likely regression if anyone touches the numbers:

- `1344 % slice_width == 0` and `1344 / slice_width == slice_count`
- `1892 % slice_height == 0`
- `bits_per_pixel & 0xf == 0` (the host rejects fractional bpp)

### 1.2 The computed PPS is byte-identical to the vendor PPS

This is the important test. `elgin_on()` does not carry a hardcoded PPS: it
packs one from `ctx->dsc` after the DSI host has filled in the rate control
fields. The test reproduces the host's completion step and compares the packed
payload against the vendor's literal 128 bytes.

Units exercised: `elgin_dsc_init()` plus the helper chain the msm host runs in
`dsi_populate_dsc_params()` (`drivers/gpu/drm/msm/dsi/dsi_host.c`) and
`drm_dsc_pps_payload_pack()`.

Arrange (this sequence must stay in lockstep with `dsi_populate_dsc_params()`;
if it drifts, this test is the thing that catches it):

```c
elgin_dsc_init(&dsc);
dsc.pic_width = 1344;          /* set by the host from mode->hdisplay */
dsc.pic_height = 1892;         /* set by the host from mode->vdisplay */
dsc.simple_422 = 0;
dsc.convert_rgb = 1;
dsc.vbr_enable = 0;
drm_dsc_set_const_params(&dsc);
drm_dsc_set_rc_buf_thresh(&dsc);
KUNIT_ASSERT_EQ(test, drm_dsc_setup_rc_params(&dsc, DRM_DSC_1_1_PRE_SCR), 0);
dsc.initial_scale_value = drm_dsc_initial_scale_value(&dsc);
dsc.line_buf_depth = dsc.bits_per_component + 1;
KUNIT_ASSERT_EQ(test, drm_dsc_compute_rc_parameters(&dsc), 0);
drm_dsc_pps_payload_pack(&pps, &dsc);
```

Fixture: `elgin_vendor_pps[128]`, transcribed verbatim from the `0x0a` write in
`qcom,mdss-dsi-on-command`:

```
11 00 00 89 30 80 07 64 05 40 03 b2 02 a0 02 a0
02 00 02 50 00 20 65 21 00 09 00 0c 00 1a 00 17
18 00 10 f0 03 0c 20 00 06 0b 0b 33 0e 1c 2a 38
46 54 62 69 70 77 79 7b 7d 7e 01 02 01 00 09 40
09 be 19 fc 19 fa 19 f8 1a 38 1a 78 1a b6 2a f6
2b 34 2b 74 3b 74 6b f4 00 ... (rest zero)
```

Assert: `KUNIT_EXPECT_MEMEQ(test, &pps, elgin_vendor_pps, sizeof(pps))`.

A single `memcmp` gives a useless failure message, so on mismatch also assert
the derived fields individually — these are the values that actually encode the
rate-control maths and each one localises a different kind of breakage:

| field | expected | PPS bytes |
| --- | --- | --- |
| `slice_chunk_size` | 672 | 14-15 |
| `initial_xmit_delay` | 512 | 16-17 |
| `initial_dec_delay` | 592 | 18-19 |
| `initial_scale_value` | 32 | 21 |
| `scale_increment_interval` | 25889 | 22-23 |
| `scale_decrement_interval` | 9 | 24-25 |
| `first_line_bpg_offset` | 12 | 27 |
| `nfl_bpg_offset` | 26 | 28-29 |
| `slice_bpg_offset` | 23 | 30-31 |
| `initial_offset` | 6144 | 32-33 |
| `final_offset` | 4336 | 34-35 |
| `rc_model_size` | 8192 | 38-39 |

Table-drive the field checks (`{ name, actual, expected }`) so one run reports
every mismatch rather than stopping at the first.

### 1.3 Negative case

Feed `drm_dsc_setup_rc_params()` a bpp/bpc pair that is not in the pre-SCR
table (e.g. `bits_per_pixel = 7 << 4`) and assert `-EINVAL`, to pin the
assumption that 8 bpp / 8 bpc is a table hit rather than something computed.
Guard with `kunit_suppress_warning()`-equivalent handling: the helper contains
a `WARN_ON_ONCE` on empty inputs, so only pass fully populated configs.

## Suite 2 — Command sequence generation

### 2.1 `elgin_on()` emits exactly the vendor on-command sequence

Unit: `elgin_on(struct elgin *ctx)`.

There is no DSI test infrastructure in `drivers/gpu/drm/tests/` today — nothing
under it references `mipi_dsi` — so a recording fake host is the first thing to
write. It is small:

```c
struct elgin_test_msg {
	u8 type;
	size_t tx_len;
	u8 tx_buf[160];        /* longest real message is the 128 byte PPS */
};

struct elgin_test_host {
	struct mipi_dsi_host host;
	struct elgin_test_msg log[64];
	unsigned int count;
	int fail_at;           /* -1 = never; else return -ETIMEDOUT on that index */
};
```

`.transfer()` appends `msg->type` and `msg->tx_buf`/`tx_len` to the log and
returns `msg->tx_len`. `.attach`/`.detach` are no-ops.

Fixture setup, per test:

- `drm_kunit_helper_alloc_device()` for a parent device.
- `mipi_dsi_host_register()` the fake host.
- Allocate a `struct mipi_dsi_device` bound to it (`channel = 0`, `lanes = 4`,
  `format = MIPI_DSI_FMT_RGB888`) — registering a real `mipi_dsi_device` needs
  an OF node, so either add a small DT overlay fixture via
  `kunit_device`/`of_overlay`, or construct the `mipi_dsi_device` directly and
  set `dsi->host`, which is all `mipi_dsi_dcs_write_buffer()` touches.
- A `struct elgin` with `.dsi` pointing at it and `.dsc` already run through
  the Suite 1.2 arrange block, so the PPS write is well defined.
- Register the fake host and device with `kunit_add_action()` teardown so
  failures don't leak.

Fixture: `elgin_expected_on[]`, one entry per vendor on-command line, each
`{ type, len, bytes }`, mechanically transcribed from the vendor
`qcom,mdss-dsi-on-command` byte table. The vendor header is 7 bytes —
`dtype, last, vc, ack, wait_ms, len_hi, len_lo` — so the transcription rule is:
take `dtype` and `len_lo` bytes of payload, and record `wait_ms` separately (see
2.3).

Assert, in order:

1. `count` equals the number of vendor entries (27).
2. For each entry, `type` and `tx_buf[0..len)` match.
3. Specifically that entries 0-3 are the unlock keys, in order:
   `7f 5a 5a`, `f0 5a 5a`, `f1 5a 5a`, `f2 5a 5a`. Regressing the key order
   silently bricks every later write, so assert it as its own expectation with
   its own message.
4. The `MIPI_DSI_PICTURE_PARAMETER_SET` (0x0a) entry is present exactly once,
   is 128 bytes, and lands after the `0x76` DHFR write and before the `0x74`
   QSYNC write — the DDIC latches the PPS at that point in the sequence.
5. The `0xb0` parameter-offset writes are each immediately followed by their
   `0xe1` VFP-trim payload (`b0 08` → `e1 00 04 4c`, `b0 10` → `e1 00 00 18`).
   These are position-dependent register windows; a reorder is silent.

Note on packet type: mainline picks short vs long DCS writes purely from the
payload length, while the vendor tree pins the type per line. Two vendor lines
(`39 ... 02 b0 70` and `39 ... 02 ec 01`) are long writes of two bytes that
mainline will emit as `MIPI_DSI_DCS_SHORT_WRITE_PARAM`. Assert on payload
bytes, and treat short-vs-long as an allowed difference, with the mapping
spelled out in a comment in the fixture so the next reader doesn't "fix" it.

### 2.2 `elgin_disable()` emits the off sequence

Same fake host. Assert two messages, `0x28` then `0x10`, and that
`dsi->mode_flags` has `MIPI_DSI_MODE_LPM` cleared for the duration and restored
on return (the vendor marks the off sequence `dsi_hs_mode`). `elgin_enable()`
gets the same treatment for its single `0x29`.

### 2.3 Delays

`mipi_dsi_msleep()` is not interceptable, so don't assert on wall time. Instead
assert the delay constants at the source level: keep the 130/20/110 ms values
as named macros in the driver and have the test compare them against the vendor
`wait_ms` header bytes (`0x82`, `0x14`, `0x6e`). This turns "somebody changed a
sleep" into a test failure without making the suite slow or flaky. That
refactor is a prerequisite for this test; the driver currently open-codes them.

### 2.4 Error propagation

Set `fail_at` to the index of each of: an unlock key, the PPS write, and the
last write. Assert `elgin_on()` returns the injected error, and that the
`mipi_dsi_multi_context` short-circuits — no messages are logged after
`fail_at`. This pins the `accum_err` contract, which is easy to break by
inserting a non-`_multi` call into the sequence.

## Not covered, and why

- Regulator ordering and reset timing in `elgin_prepare()`: needs a fake
  regulator and would only re-assert the `msleep()` constants. Low value
  relative to the mocking cost.
- `elgin_get_modes()`: a one-line wrapper around
  `drm_connector_helper_get_modes_fixed()`, already covered by
  `drm_probe_helper_test.c`. The mode numbers themselves are worth one cheap
  assertion though: `elgin_mode.clock == 169862` and
  `htotal * vtotal * 60 / 1000 == clock`, guarding an arithmetic slip in the
  porch expressions.
- The 90 Hz timing and the DHFR switch: not implemented in the driver.
- Backlight: `elgin_bl_update_status()` is a thin wrapper around
  `mipi_dsi_dcs_set_display_brightness_large()`. One test is still worth it —
  assert the DDIC receives DBV big endian, i.e. brightness 0x587 produces
  payload `51 05 87`, matching the vendor default write and the downstream
  `qcom,mdss-dsi-bl-inverted-dbv` flag. Getting this byte order backwards is a
  plausible mistake that shows up as garbled brightness, not a crash.
