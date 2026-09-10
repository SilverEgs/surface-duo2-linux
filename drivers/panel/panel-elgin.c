// SPDX-License-Identifier: GPL-2.0-only
/*
 * Microsoft Surface Duo 2 "elgin" 1344x1892 command mode MIPI-DSI OLED panel
 *
 * The Surface Duo 2 (Qualcomm SM8350) carries two of these panels, one on each
 * DSI controller.  Both have identical geometry and an identical initialisation
 * sequence; they only differ in DDIC stepping (rev "c3" on DSI0, rev "r2" on
 * DSI1), so a single compatible covers both.
 *
 * The DDIC vendor is not confirmed.  The register set (7Fh UCS key, F0h/F1h/F2h
 * MCS level keys, B0h parameter offset) is Samsung-style, but the part has not
 * been matched against a datasheet, hence the panel-module ("elgin") based
 * naming rather than a "<ddic-vendor>,<part>" compatible.
 *
 * Register semantics and timings below are transcribed from the vendor
 * downstream device tree, qcom,mdss-dsi-{on,off}-command of
 * surface_elgin_dsi0_c3_cmd_mp.dtsi (and the identical dsi1_r2 variant), from
 * the surface-duo-oss-sm8350.11.display-devicetree tree.  Registers whose
 * function the vendor tree does not explain are marked "unknown" rather than
 * guessed at.
 *
 * Copyright (c) 2026 Linux Surface Duo 2 porting effort
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

/* Unlock keys: written as <reg> 5Ah 5Ah to open a register access level. */
#define ELGIN_KEY_UCS			0x7f
#define ELGIN_KEY_MCS_LV1		0xf0
#define ELGIN_KEY_MCS_LV2		0xf1
#define ELGIN_KEY_MCS_LV3		0xf2

#define ELGIN_REG_FLASH_ACCESS		0xe7
#define ELGIN_REG_DSC_EN		0x02
#define ELGIN_REG_DSC_MODE		0x59
#define ELGIN_REG_MLPIS			0x57
#define ELGIN_REG_MLPIS_AOD		0x58
#define ELGIN_REG_DHFR			0x76
#define ELGIN_REG_QSYNC			0x74
#define ELGIN_REG_FORCE_DCS_TE		0xe5
#define ELGIN_REG_PARAM_OFFSET		0xb0
#define ELGIN_REG_VFP_TRIM		0xe1
#define ELGIN_REG_FLASH_CTRL		0xec

/* DHFR (dynamic high frame rate) selector, driven by the ST PMIC. */
#define ELGIN_DHFR_60HZ			0x00
#define ELGIN_DHFR_90HZ			0x01

/* Brightness control block on, no dimming, no backlight-off */
#define ELGIN_CTRL_DISPLAY_BCTRL	0x20

/* Peak luminance control, rate 1 */
#define ELGIN_POWER_SAVE_PLC_RATE1	0x04

/*
 * DBV (display brightness value) is 11 bit.  0x587 is the vendor default and
 * corresponds to roughly 350 nits.  The DDIC expects the DBV big endian on the
 * wire, which downstream expresses as "qcom,mdss-dsi-bl-inverted-dbv" and
 * mainline as mipi_dsi_dcs_set_display_brightness_large().
 */
#define ELGIN_DBV_MAX			2047
#define ELGIN_DBV_DEFAULT		0x587

struct elgin {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

/*
 * The vendor tree points the panel at the generic lahaina dsi_panel_pwr_supply
 * node, which lists vddio and vdd (plus the lab/ibb LCD bias rails, which do
 * not apply to this OLED module).  ELVDD/ELVSS are not described there and are
 * assumed to be handled outside this driver.
 */
enum elgin_supply {
	ELGIN_SUPPLY_VDDIO,
	ELGIN_SUPPLY_VDD,
	ELGIN_SUPPLY_MAX
};

static const struct regulator_bulk_data elgin_supplies[ELGIN_SUPPLY_MAX] = {
	[ELGIN_SUPPLY_VDDIO] = { .supply = "vddio" },	/* 1.8 V */
	[ELGIN_SUPPLY_VDD] = { .supply = "vdd" },	/* 3.0 V */
};

static inline struct elgin *to_elgin(struct drm_panel *panel)
{
	return container_of(panel, struct elgin, panel);
}

/*
 * DSC 1.1, one encoder, two 672x946 slices per line.  The remaining fields
 * (rate control, chunk size, initial delays) are computed by the DSI host from
 * these when the panel is attached.
 */
static void elgin_dsc_init(struct drm_dsc_config *dsc)
{
	dsc->dsc_version_major = 1;
	dsc->dsc_version_minor = 1;
	dsc->slice_width = 672;
	dsc->slice_height = 946;
	dsc->slice_count = 1344 / dsc->slice_width;
	dsc->bits_per_component = 8;
	dsc->bits_per_pixel = 8 << 4;
	dsc->block_pred_enable = true;
}

static void elgin_reset(struct elgin *ctx)
{
	/* Vendor qcom,mdss-dsi-reset-sequence = <0 10>, <1 10> */
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(10);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(10);
}

static int elgin_on(struct elgin *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	struct drm_dsc_picture_parameter_set pps;

	/* Open user and manufacturer command sets */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_KEY_UCS, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_KEY_MCS_LV1, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_KEY_MCS_LV2, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_KEY_MCS_LV3, 0x5a, 0x5a);

	/* Flash access */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_FLASH_ACCESS, 0x00);

	/*
	 * Enable DSC and select command mode DSC.  The DDIC is configured
	 * through these two vendor registers instead of the standard DSI
	 * compression mode packet, so mipi_dsi_compression_mode_multi() is
	 * deliberately not used here.  If the panel ever comes up with a
	 * corrupt image, sending it after the PPS below is the first thing to
	 * try.
	 */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_DSC_EN, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_DSC_MODE, 0x00);

	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0, 1343);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0, 1891);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
				     ELGIN_DBV_DEFAULT >> 8,
				     ELGIN_DBV_DEFAULT & 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     ELGIN_CTRL_DISPLAY_BCTRL);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE,
				     ELGIN_POWER_SAVE_PLC_RATE1);

	/*
	 * mLPIS (vendor name), and the same block for AoD with every stage
	 * disabled.  The payload fields are not documented by the vendor tree,
	 * so both are transcribed verbatim.
	 */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_MLPIS,
				     0x20, 0x01, 0x00, 0xf8, 0xff, 0xf0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_MLPIS_AOD,
				     0x00, 0x00, 0x00, 0xf8, 0xff, 0xfc);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_DHFR, ELGIN_DHFR_60HZ);

	/*
	 * The vendor tree carries a hardcoded 128 byte PPS.  It is reproduced
	 * bit for bit by drm_dsc_pps_payload_pack() once the msm DSI host has
	 * filled in the computed rate control parameters at attach time, so
	 * pack it from ctx->dsc rather than open coding the byte array.
	 */
	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);
	mipi_dsi_picture_parameter_set_multi(&dsi_ctx, &pps);

	/* QSYNC_EN = 1, EXT_TRIG = 1, QSYNC_TO = 208 (52 x 4) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_QSYNC,
				     0x05, 0x00, 0x34);

	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_set_tear_scanline_multi(&dsi_ctx, 0);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 130);

	/* Force DCS TE */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_FORCE_DCS_TE,
				     0x00, 0x20);

	/* VFP trimming: 60 Hz parameters 9-11, 1212 (0x4bc) -> 1100 (0x44c) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_PARAM_OFFSET, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_VFP_TRIM,
				     0x00, 0x04, 0x4c);

	/* VFP trimming: 90 Hz parameters 17-19, 168 (0xa8) -> 24 (0x18) */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_PARAM_OFFSET, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_VFP_TRIM,
				     0x00, 0x00, 0x18);

	/* Parameter offset 0x70, then flash control - function unknown */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_PARAM_OFFSET, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, ELGIN_REG_FLASH_CTRL, 0x01);

	return dsi_ctx.accum_err;
}

static int elgin_prepare(struct drm_panel *panel)
{
	struct elgin *ctx = to_elgin(panel);
	int ret;

	ret = regulator_enable(ctx->supplies[ELGIN_SUPPLY_VDDIO].consumer);
	if (ret < 0)
		return ret;
	/* Vendor qcom,supply-post-on-sleep for vddio */
	msleep(20);

	ret = regulator_enable(ctx->supplies[ELGIN_SUPPLY_VDD].consumer);
	if (ret < 0)
		goto err_vdd;

	elgin_reset(ctx);

	ret = elgin_on(ctx);
	if (ret < 0)
		goto err_on;

	return 0;

err_on:
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_disable(ctx->supplies[ELGIN_SUPPLY_VDD].consumer);
err_vdd:
	regulator_disable(ctx->supplies[ELGIN_SUPPLY_VDDIO].consumer);
	return ret;
}

static int elgin_unprepare(struct drm_panel *panel)
{
	struct elgin *ctx = to_elgin(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(elgin_supplies), ctx->supplies);

	return 0;
}

static int elgin_enable(struct drm_panel *panel)
{
	struct elgin *ctx = to_elgin(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	/* Vendor qcom,mdss-dsi-post-panel-on-command, sent in HS mode */
	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return dsi_ctx.accum_err;
}

static int elgin_disable(struct drm_panel *panel)
{
	struct elgin *ctx = to_elgin(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	/* Vendor qcom,mdss-dsi-off-command-state = "dsi_hs_mode" */
	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 110);

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return dsi_ctx.accum_err;
}

/*
 * Only the 60 Hz timing is exposed.  The panel also supports 90 Hz (vendor
 * timing@1), but switching needs the DHFR register write plus a different DSI
 * bit clock (760 MHz instead of 700 MHz), which is not wired up yet.
 */
static const struct drm_display_mode elgin_mode = {
	.clock = (1344 + 32 + 32 + 32) * (1892 + 32 + 10 + 32) * 60 / 1000,
	.hdisplay = 1344,
	.hsync_start = 1344 + 32,
	.hsync_end = 1344 + 32 + 32,
	.htotal = 1344 + 32 + 32 + 32,
	.vdisplay = 1892,
	.vsync_start = 1892 + 32,
	.vsync_end = 1892 + 32 + 10,
	.vtotal = 1892 + 32 + 10 + 32,
	.width_mm = 85,
	.height_mm = 120,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int elgin_get_modes(struct drm_panel *panel,
			   struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &elgin_mode);
}

static const struct drm_panel_funcs elgin_panel_funcs = {
	.prepare = elgin_prepare,
	.unprepare = elgin_unprepare,
	.enable = elgin_enable,
	.disable = elgin_disable,
	.get_modes = elgin_get_modes,
};

static int elgin_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	if (ret < 0)
		return ret;

	return 0;
}

static int elgin_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	if (ret < 0)
		return ret;

	return brightness;
}

static const struct backlight_ops elgin_bl_ops = {
	.update_status = elgin_bl_update_status,
	.get_brightness = elgin_bl_get_brightness,
};

static struct backlight_device *elgin_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = ELGIN_DBV_DEFAULT,
		.max_brightness = ELGIN_DBV_MAX,
		.scale = BACKLIGHT_SCALE_NON_LINEAR,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &elgin_bl_ops, &props);
}

static int elgin_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct elgin *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct elgin, panel, &elgin_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(elgin_supplies),
					    elgin_supplies, &ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = elgin_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	elgin_dsc_init(&ctx->dsc);
	dsi->dsc = &ctx->dsc;

	drm_panel_add(&ctx->panel);

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void elgin_remove(struct mipi_dsi_device *dsi)
{
	struct elgin *ctx = mipi_dsi_get_drvdata(dsi);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id elgin_of_match[] = {
	{ .compatible = "microsoft,elgin" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, elgin_of_match);

static struct mipi_dsi_driver elgin_driver = {
	.probe = elgin_probe,
	.remove = elgin_remove,
	.driver = {
		.name = "panel-elgin",
		.of_match_table = elgin_of_match,
	},
};
module_mipi_dsi_driver(elgin_driver);

MODULE_DESCRIPTION("DRM driver for the Surface Duo 2 elgin cmd mode DSI panel");
MODULE_LICENSE("GPL");
