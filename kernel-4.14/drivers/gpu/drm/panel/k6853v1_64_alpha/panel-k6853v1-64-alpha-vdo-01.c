/*
 * Copyright (c) 2015 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/backlight.h>
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_panel_ext.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_drm_graphics_base.h"
#endif

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
#include "../mediatek/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"
#endif

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;

	bool prepared;
	bool enabled;

	int error;
};

#define lcm_dcs_write_seq(ctx, seq...) \
({\
	const u8 d[] = { seq };\
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, "DCS sequence too big for stack");\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

#define lcm_dcs_write_seq_static(ctx, seq...) \
({\
	static const u8 d[] = { seq };\
	lcm_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

#ifdef PANEL_SUPPORT_READBACK
static int lcm_dcs_read(struct lcm *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return 0;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_info(ctx->dev, "error %d reading dcs seq:(%#x)\n",
		ret, cmd);
		ctx->error = ret;
	}

	return ret;
}

static void lcm_panel_get_data(struct lcm *ctx)
{
	u8 buffer[3] = {0};
	static int ret;

	if (ret == 0) {
		ret = lcm_dcs_read(ctx,  0x0A, buffer, 1);
		dev_info(ctx->dev, "return %d data(0x%08x) to dsi engine\n",
			ret, buffer[0] | (buffer[1] << 8));
	}
}
#endif

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
static struct regulator *disp_bias_pos;
static struct regulator *disp_bias_neg;


static int lcm_panel_bias_regulator_init(void)
{
	static int regulator_inited;
	int ret = 0;

	if (regulator_inited)
		return ret;

	/* please only get regulator once in a driver */
	disp_bias_pos = regulator_get(NULL, "dsv_pos");
	if (IS_ERR(disp_bias_pos)) { /* handle return value */
		ret = PTR_ERR(disp_bias_pos);
		dev_info("get dsv_pos fail, error: %d\n", ret);
		return ret;
	}

	disp_bias_neg = regulator_get(NULL, "dsv_neg");
	if (IS_ERR(disp_bias_neg)) { /* handle return value */
		ret = PTR_ERR(disp_bias_neg);
		dev_info("get dsv_neg fail, error: %d\n", ret);
		return ret;
	}

	regulator_inited = 1;
	return ret; /* must be 0 */

}

static int lcm_panel_bias_enable(void)
{
	int ret = 0;
	int retval = 0;

	lcm_panel_bias_regulator_init();

	/* set voltage with min & max*/
	ret = regulator_set_voltage(disp_bias_pos, 5400000, 5400000);
	if (ret < 0)
		dev_info("set voltage disp_bias_pos fail, ret = %d\n", ret);
	retval |= ret;

	ret = regulator_set_voltage(disp_bias_neg, 5400000, 5400000);
	if (ret < 0)
		dev_info("set voltage disp_bias_neg fail, ret = %d\n", ret);
	retval |= ret;

	/* enable regulator */
	ret = regulator_enable(disp_bias_pos);
	if (ret < 0)
		dev_info("enable regulator disp_bias_pos fail, ret = %d\n",
			ret);
	retval |= ret;

	ret = regulator_enable(disp_bias_neg);
	if (ret < 0)
		dev_info("enable regulator disp_bias_neg fail, ret = %d\n",
			ret);
	retval |= ret;

	return retval;
}

static int lcm_panel_bias_disable(void)
{
	int ret = 0;
	int retval = 0;

	lcm_panel_bias_regulator_init();

	ret = regulator_disable(disp_bias_neg);
	if (ret < 0)
		dev_info("disable regulator disp_bias_neg fail, ret = %d\n",
			ret);
	retval |= ret;

	ret = regulator_disable(disp_bias_pos);
	if (ret < 0)
		dev_info("disable regulator disp_bias_pos fail, ret = %d\n",
			ret);
	retval |= ret;

	return retval;
}
#endif

static void lcm_panel_init(struct lcm *ctx)
{
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, 0);
	udelay(15 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(10 * 1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	udelay(10 * 1000);
	gpiod_set_value(ctx->reset_gpio, 1);
	udelay(10 * 1000);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	lcm_dcs_write_seq_static(ctx, 0xFF, 0X10);
	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	lcm_dcs_write_seq_static(ctx, 0XB0, 0X00);
	//DSC ON && set PPS
	lcm_dcs_write_seq_static(ctx, 0xC0, 0x03);
	lcm_dcs_write_seq_static(ctx, 0xC1, 0x89, 0x28, 0x00, 0x08, 0x00, 0xAA,
				0x02, 0x0E, 0x00, 0x2B, 0x00, 0x07, 0x0D, 0xB7,
				0x0C, 0xB7);
	lcm_dcs_write_seq_static(ctx, 0xC2, 0x1B, 0XA0);

	lcm_dcs_write_seq_static(ctx, 0xFF, 0X20);
	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	lcm_dcs_write_seq_static(ctx, 0X01, 0X66);
	lcm_dcs_write_seq_static(ctx, 0X06, 0X40);
	lcm_dcs_write_seq_static(ctx, 0X07, 0X38);
	lcm_dcs_write_seq_static(ctx, 0X1B, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X69, 0X91);
	lcm_dcs_write_seq_static(ctx, 0X95, 0XD1);
	lcm_dcs_write_seq_static(ctx, 0X96, 0XD1);
	lcm_dcs_write_seq_static(ctx, 0XF2, 0X64);
	lcm_dcs_write_seq_static(ctx, 0XF4, 0X64);
	lcm_dcs_write_seq_static(ctx, 0XF6, 0X64);
	lcm_dcs_write_seq_static(ctx, 0XF8, 0X64);

	lcm_dcs_write_seq_static(ctx, 0xFF, 0X24);
	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	lcm_dcs_write_seq_static(ctx, 0X01, 0X0F);
	lcm_dcs_write_seq_static(ctx, 0X03, 0X0C);
	lcm_dcs_write_seq_static(ctx, 0X05, 0X1D);
	lcm_dcs_write_seq_static(ctx, 0X08, 0X2F);
	lcm_dcs_write_seq_static(ctx, 0X09, 0X2E);
	lcm_dcs_write_seq_static(ctx, 0X0A, 0X2D);
	lcm_dcs_write_seq_static(ctx, 0X0B, 0X2C);
	lcm_dcs_write_seq_static(ctx, 0X11, 0X17);
	lcm_dcs_write_seq_static(ctx, 0X12, 0X13);
	lcm_dcs_write_seq_static(ctx, 0X13, 0X15);
	lcm_dcs_write_seq_static(ctx, 0X15, 0X14);
	lcm_dcs_write_seq_static(ctx, 0X16, 0X16);
	lcm_dcs_write_seq_static(ctx, 0X17, 0X18);
	lcm_dcs_write_seq_static(ctx, 0X1B, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X1D, 0X1D);
	lcm_dcs_write_seq_static(ctx, 0X20, 0X2F);
	lcm_dcs_write_seq_static(ctx, 0X21, 0X2E);
	lcm_dcs_write_seq_static(ctx, 0X22, 0X2D);
	lcm_dcs_write_seq_static(ctx, 0X23, 0X2C);
	lcm_dcs_write_seq_static(ctx, 0X29, 0X17);
	lcm_dcs_write_seq_static(ctx, 0X2A, 0X13);
	lcm_dcs_write_seq_static(ctx, 0X2B, 0X15);
	lcm_dcs_write_seq_static(ctx, 0X2F, 0X14);
	lcm_dcs_write_seq_static(ctx, 0X30, 0X16);
	lcm_dcs_write_seq_static(ctx, 0X31, 0X18);
	lcm_dcs_write_seq_static(ctx, 0X32, 0X04);
	lcm_dcs_write_seq_static(ctx, 0X34, 0X10);
	lcm_dcs_write_seq_static(ctx, 0X35, 0X1F);
	lcm_dcs_write_seq_static(ctx, 0X36, 0X1F);
	lcm_dcs_write_seq_static(ctx, 0X37, 0X20);
	lcm_dcs_write_seq_static(ctx, 0X4D, 0X1B);
	lcm_dcs_write_seq_static(ctx, 0X4E, 0X4B);
	lcm_dcs_write_seq_static(ctx, 0X4F, 0X4B);
	lcm_dcs_write_seq_static(ctx, 0X53, 0X4B);
	lcm_dcs_write_seq_static(ctx, 0X71, 0X30);
	lcm_dcs_write_seq_static(ctx, 0X79, 0X11);
	lcm_dcs_write_seq_static(ctx, 0X7A, 0X82);
	lcm_dcs_write_seq_static(ctx, 0X7B, 0X96);
	lcm_dcs_write_seq_static(ctx, 0X7D, 0X04);
	lcm_dcs_write_seq_static(ctx, 0X80, 0X04);
	lcm_dcs_write_seq_static(ctx, 0X81, 0X04);
	lcm_dcs_write_seq_static(ctx, 0X82, 0X13);
	lcm_dcs_write_seq_static(ctx, 0X84, 0X31);
	lcm_dcs_write_seq_static(ctx, 0X85, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X86, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X87, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X90, 0X13);
	lcm_dcs_write_seq_static(ctx, 0X92, 0X31);
	lcm_dcs_write_seq_static(ctx, 0X93, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X94, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X95, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X9C, 0XF4);
	lcm_dcs_write_seq_static(ctx, 0X9D, 0X01);
	lcm_dcs_write_seq_static(ctx, 0XA0, 0X16);
	lcm_dcs_write_seq_static(ctx, 0XA2, 0X16);
	lcm_dcs_write_seq_static(ctx, 0XA3, 0X02);
	lcm_dcs_write_seq_static(ctx, 0XA4, 0X04);
	lcm_dcs_write_seq_static(ctx, 0XA5, 0X04);
	lcm_dcs_write_seq_static(ctx, 0XC9, 0X00);
	lcm_dcs_write_seq_static(ctx, 0XD9, 0X80);
	lcm_dcs_write_seq_static(ctx, 0XE9, 0X02);

	lcm_dcs_write_seq_static(ctx, 0XFF, 0X25);
	lcm_dcs_write_seq_static(ctx, 0XFB, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X19, 0XE4);
	lcm_dcs_write_seq_static(ctx, 0X21, 0X40);
	lcm_dcs_write_seq_static(ctx, 0X66, 0XD8);
	lcm_dcs_write_seq_static(ctx, 0X68, 0X50);
	lcm_dcs_write_seq_static(ctx, 0X69, 0X10);
	lcm_dcs_write_seq_static(ctx, 0X6B, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X6D, 0X0D);
	lcm_dcs_write_seq_static(ctx, 0X6E, 0X48);
	lcm_dcs_write_seq_static(ctx, 0X72, 0X41);
	lcm_dcs_write_seq_static(ctx, 0X73, 0X4A);
	lcm_dcs_write_seq_static(ctx, 0X74, 0XD0);
	lcm_dcs_write_seq_static(ctx, 0X77, 0X62);
	lcm_dcs_write_seq_static(ctx, 0X79, 0X81);
	lcm_dcs_write_seq_static(ctx, 0X7D, 0X03);
	lcm_dcs_write_seq_static(ctx, 0X7E, 0X15);
	lcm_dcs_write_seq_static(ctx, 0X7F, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X84, 0X4D);
	lcm_dcs_write_seq_static(ctx, 0XCF, 0X80);
	lcm_dcs_write_seq_static(ctx, 0XD6, 0X80);
	lcm_dcs_write_seq_static(ctx, 0XD7, 0X80);
	lcm_dcs_write_seq_static(ctx, 0XEF, 0X20);
	lcm_dcs_write_seq_static(ctx, 0XF0, 0X84);

	lcm_dcs_write_seq_static(ctx, 0XFF, 0X26);
	lcm_dcs_write_seq_static(ctx, 0XFB, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X80, 0X05);
	lcm_dcs_write_seq_static(ctx, 0X81, 0X16);
	lcm_dcs_write_seq_static(ctx, 0X83, 0X03);
	lcm_dcs_write_seq_static(ctx, 0X84, 0X03);
	lcm_dcs_write_seq_static(ctx, 0X85, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X86, 0X03);
	lcm_dcs_write_seq_static(ctx, 0X87, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X8A, 0X1A);
	lcm_dcs_write_seq_static(ctx, 0X8B, 0X11);
	lcm_dcs_write_seq_static(ctx, 0X8C, 0X24);
	lcm_dcs_write_seq_static(ctx, 0X8E, 0X42);
	lcm_dcs_write_seq_static(ctx, 0X8F, 0X11);
	lcm_dcs_write_seq_static(ctx, 0X90, 0X11);
	lcm_dcs_write_seq_static(ctx, 0X91, 0X11);
	lcm_dcs_write_seq_static(ctx, 0X9A, 0X81);
	lcm_dcs_write_seq_static(ctx, 0X9B, 0X03);
	lcm_dcs_write_seq_static(ctx, 0X9C, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X9D, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X9E, 0X00);

	lcm_dcs_write_seq_static(ctx, 0XFF, 0X27);
	lcm_dcs_write_seq_static(ctx, 0XFB, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X01, 0X60);
	lcm_dcs_write_seq_static(ctx, 0X20, 0X81);
	lcm_dcs_write_seq_static(ctx, 0X21, 0XEA);
	lcm_dcs_write_seq_static(ctx, 0X25, 0X82);
	lcm_dcs_write_seq_static(ctx, 0X26, 0X1F);
	lcm_dcs_write_seq_static(ctx, 0X6E, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X6F, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X70, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X71, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X72, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X75, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X76, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X77, 0X00);
	lcm_dcs_write_seq_static(ctx, 0X7D, 0X09);
	lcm_dcs_write_seq_static(ctx, 0X7E, 0X5F);
	lcm_dcs_write_seq_static(ctx, 0X80, 0X23);
	lcm_dcs_write_seq_static(ctx, 0X82, 0X09);
	lcm_dcs_write_seq_static(ctx, 0X83, 0X5F);
	lcm_dcs_write_seq_static(ctx, 0X88, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X89, 0X10);
	lcm_dcs_write_seq_static(ctx, 0XA5, 0X10);
	lcm_dcs_write_seq_static(ctx, 0XA6, 0X23);
	lcm_dcs_write_seq_static(ctx, 0XA7, 0X01);
	lcm_dcs_write_seq_static(ctx, 0XB6, 0X40);
	lcm_dcs_write_seq_static(ctx, 0XE3, 0X02);
	lcm_dcs_write_seq_static(ctx, 0XE4, 0XE0);
	lcm_dcs_write_seq_static(ctx, 0XE5, 0X01);
	lcm_dcs_write_seq_static(ctx, 0XE6, 0X70);
	lcm_dcs_write_seq_static(ctx, 0XE9, 0X03);
	lcm_dcs_write_seq_static(ctx, 0XEA, 0X2F);
	lcm_dcs_write_seq_static(ctx, 0XEB, 0X01);
	lcm_dcs_write_seq_static(ctx, 0XEC, 0X98);

	lcm_dcs_write_seq_static(ctx, 0XFF, 0X2A);
	lcm_dcs_write_seq_static(ctx, 0XFB, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X00, 0X91);
	lcm_dcs_write_seq_static(ctx, 0X03, 0X20);
	lcm_dcs_write_seq_static(ctx, 0X07, 0X52);
	lcm_dcs_write_seq_static(ctx, 0X0A, 0X60);
	lcm_dcs_write_seq_static(ctx, 0X0C, 0X06);
	lcm_dcs_write_seq_static(ctx, 0X0D, 0X40);
	lcm_dcs_write_seq_static(ctx, 0X0E, 0X02);
	lcm_dcs_write_seq_static(ctx, 0X0F, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X11, 0X58);
	lcm_dcs_write_seq_static(ctx, 0X15, 0X0E);
	lcm_dcs_write_seq_static(ctx, 0X16, 0X79);
	lcm_dcs_write_seq_static(ctx, 0X19, 0X0D);
	lcm_dcs_write_seq_static(ctx, 0X1A, 0XF2);
	lcm_dcs_write_seq_static(ctx, 0X1B, 0X14);
	lcm_dcs_write_seq_static(ctx, 0X1D, 0X36);
	lcm_dcs_write_seq_static(ctx, 0X1E, 0X55);
	lcm_dcs_write_seq_static(ctx, 0X1F, 0X55);
	lcm_dcs_write_seq_static(ctx, 0X20, 0X55);
	lcm_dcs_write_seq_static(ctx, 0X28, 0X0A);
	lcm_dcs_write_seq_static(ctx, 0X29, 0X0B);
	lcm_dcs_write_seq_static(ctx, 0X2A, 0X4B);
	lcm_dcs_write_seq_static(ctx, 0X2B, 0X05);
	lcm_dcs_write_seq_static(ctx, 0X2D, 0X08);
	lcm_dcs_write_seq_static(ctx, 0X2F, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X30, 0X47);
	lcm_dcs_write_seq_static(ctx, 0X31, 0X23);
	lcm_dcs_write_seq_static(ctx, 0X33, 0X25);
	lcm_dcs_write_seq_static(ctx, 0X34, 0XFF);
	lcm_dcs_write_seq_static(ctx, 0X35, 0X2C);
	lcm_dcs_write_seq_static(ctx, 0X36, 0X75);
	lcm_dcs_write_seq_static(ctx, 0X37, 0XFB);
	lcm_dcs_write_seq_static(ctx, 0X38, 0X2E);
	lcm_dcs_write_seq_static(ctx, 0X39, 0X73);
	lcm_dcs_write_seq_static(ctx, 0X3A, 0X47);
	lcm_dcs_write_seq_static(ctx, 0X46, 0X40);
	lcm_dcs_write_seq_static(ctx, 0X47, 0X02);
	lcm_dcs_write_seq_static(ctx, 0X4A, 0XF0);
	lcm_dcs_write_seq_static(ctx, 0X4E, 0X0E);
	lcm_dcs_write_seq_static(ctx, 0X4F, 0X8B);
	lcm_dcs_write_seq_static(ctx, 0X52, 0X0E);
	lcm_dcs_write_seq_static(ctx, 0X53, 0X04);
	lcm_dcs_write_seq_static(ctx, 0X54, 0X14);
	lcm_dcs_write_seq_static(ctx, 0X56, 0X36);
	lcm_dcs_write_seq_static(ctx, 0X57, 0X80);
	lcm_dcs_write_seq_static(ctx, 0X58, 0X80);
	lcm_dcs_write_seq_static(ctx, 0X59, 0X80);
	lcm_dcs_write_seq_static(ctx, 0X60, 0X80);
	lcm_dcs_write_seq_static(ctx, 0X61, 0X0A);
	lcm_dcs_write_seq_static(ctx, 0X62, 0X03);
	lcm_dcs_write_seq_static(ctx, 0X63, 0XED);
	lcm_dcs_write_seq_static(ctx, 0X65, 0X05);
	lcm_dcs_write_seq_static(ctx, 0X66, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X67, 0X04);
	lcm_dcs_write_seq_static(ctx, 0X68, 0X4D);
	lcm_dcs_write_seq_static(ctx, 0X6A, 0X0A);
	lcm_dcs_write_seq_static(ctx, 0X6B, 0XC9);
	lcm_dcs_write_seq_static(ctx, 0X6C, 0X1F);
	lcm_dcs_write_seq_static(ctx, 0X6D, 0XE3);
	lcm_dcs_write_seq_static(ctx, 0X6E, 0XC6);
	lcm_dcs_write_seq_static(ctx, 0X6F, 0X20);
	lcm_dcs_write_seq_static(ctx, 0X70, 0XE2);
	lcm_dcs_write_seq_static(ctx, 0X71, 0X04);
	lcm_dcs_write_seq_static(ctx, 0X7A, 0X04);
	lcm_dcs_write_seq_static(ctx, 0X7B, 0X40);
	lcm_dcs_write_seq_static(ctx, 0X7C, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X7D, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X7F, 0XE0);
	lcm_dcs_write_seq_static(ctx, 0X83, 0X0E);
	lcm_dcs_write_seq_static(ctx, 0X84, 0X8B);
	lcm_dcs_write_seq_static(ctx, 0X87, 0X0E);
	lcm_dcs_write_seq_static(ctx, 0X88, 0X04);
	lcm_dcs_write_seq_static(ctx, 0X89, 0X14);
	lcm_dcs_write_seq_static(ctx, 0X8B, 0X36);
	lcm_dcs_write_seq_static(ctx, 0X8C, 0X40);
	lcm_dcs_write_seq_static(ctx, 0X8D, 0X40);
	lcm_dcs_write_seq_static(ctx, 0X8E, 0X40);
	lcm_dcs_write_seq_static(ctx, 0X95, 0X80);
	lcm_dcs_write_seq_static(ctx, 0X96, 0X0A);
	lcm_dcs_write_seq_static(ctx, 0X97, 0X12);
	lcm_dcs_write_seq_static(ctx, 0X98, 0X92);
	lcm_dcs_write_seq_static(ctx, 0X9A, 0X0A);
	lcm_dcs_write_seq_static(ctx, 0X9B, 0X02);
	lcm_dcs_write_seq_static(ctx, 0X9C, 0X49);
	lcm_dcs_write_seq_static(ctx, 0X9D, 0X98);
	lcm_dcs_write_seq_static(ctx, 0X9F, 0X5F);
	lcm_dcs_write_seq_static(ctx, 0XA0, 0XFF);
	lcm_dcs_write_seq_static(ctx, 0XA2, 0X3A);
	lcm_dcs_write_seq_static(ctx, 0XA3, 0XD9);
	lcm_dcs_write_seq_static(ctx, 0XA4, 0XFA);
	lcm_dcs_write_seq_static(ctx, 0XA5, 0X3C);
	lcm_dcs_write_seq_static(ctx, 0XA6, 0XD7);
	lcm_dcs_write_seq_static(ctx, 0XA7, 0X49);

	lcm_dcs_write_seq_static(ctx, 0XFF, 0X2C);
	lcm_dcs_write_seq_static(ctx, 0XFB, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X00, 0X02);
	lcm_dcs_write_seq_static(ctx, 0X01, 0X02);
	lcm_dcs_write_seq_static(ctx, 0X02, 0X02);
	lcm_dcs_write_seq_static(ctx, 0X03, 0X16);
	lcm_dcs_write_seq_static(ctx, 0X04, 0X16);
	lcm_dcs_write_seq_static(ctx, 0X05, 0X16);
	lcm_dcs_write_seq_static(ctx, 0X0D, 0X1F);
	lcm_dcs_write_seq_static(ctx, 0X0E, 0X1F);
	lcm_dcs_write_seq_static(ctx, 0X16, 0X1B);
	lcm_dcs_write_seq_static(ctx, 0X17, 0X4B);
	lcm_dcs_write_seq_static(ctx, 0X18, 0X4B);
	lcm_dcs_write_seq_static(ctx, 0X19, 0X4B);
	lcm_dcs_write_seq_static(ctx, 0X2A, 0X03);
	lcm_dcs_write_seq_static(ctx, 0X4D, 0X16);
	lcm_dcs_write_seq_static(ctx, 0X4E, 0X03);
	lcm_dcs_write_seq_static(ctx, 0X53, 0X02);
	lcm_dcs_write_seq_static(ctx, 0X54, 0X02);
	lcm_dcs_write_seq_static(ctx, 0X55, 0X02);
	lcm_dcs_write_seq_static(ctx, 0X56, 0X0F);
	lcm_dcs_write_seq_static(ctx, 0X58, 0X0F);
	lcm_dcs_write_seq_static(ctx, 0X59, 0X0F);
	lcm_dcs_write_seq_static(ctx, 0X61, 0X1F);
	lcm_dcs_write_seq_static(ctx, 0X62, 0X1F);
	lcm_dcs_write_seq_static(ctx, 0X6A, 0X15);
	lcm_dcs_write_seq_static(ctx, 0X6B, 0X37);
	lcm_dcs_write_seq_static(ctx, 0X6C, 0X37);
	lcm_dcs_write_seq_static(ctx, 0X6D, 0X37);
	lcm_dcs_write_seq_static(ctx, 0X7E, 0X03);
	lcm_dcs_write_seq_static(ctx, 0X9D, 0X10);
	lcm_dcs_write_seq_static(ctx, 0X9E, 0X03);

	lcm_dcs_write_seq_static(ctx, 0XFF, 0XE0);
	lcm_dcs_write_seq_static(ctx, 0XFB, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X35, 0X82);

	lcm_dcs_write_seq_static(ctx, 0XFF, 0XF0);
	lcm_dcs_write_seq_static(ctx, 0XFB, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X5A, 0X00);

	lcm_dcs_write_seq_static(ctx, 0XFF, 0XD0);
	lcm_dcs_write_seq_static(ctx, 0XFB, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X53, 0X22);
	lcm_dcs_write_seq_static(ctx, 0X54, 0X02);

	lcm_dcs_write_seq_static(ctx, 0XFF, 0XC0);
	lcm_dcs_write_seq_static(ctx, 0XFB, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X9C, 0X11);
	lcm_dcs_write_seq_static(ctx, 0X9D, 0X11);

	//120HZ VESA DSC
	lcm_dcs_write_seq_static(ctx, 0XFF, 0X25);
	lcm_dcs_write_seq_static(ctx, 0XFB, 0X01);
	lcm_dcs_write_seq_static(ctx, 0X18, 0X22);

	lcm_dcs_write_seq_static(ctx, 0XFF, 0X10);
	lcm_dcs_write_seq_static(ctx, 0XFB, 0X01);
	lcm_dcs_write_seq_static(ctx, 0XC0, 0X03);
	lcm_dcs_write_seq_static(ctx, 0x51, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x35, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x53, 0x24);
	lcm_dcs_write_seq_static(ctx, 0x55, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xFF, 0x10);

	lcm_dcs_write_seq_static(ctx, 0x11);
	msleep(140);
	lcm_dcs_write_seq_static(ctx, 0x29);
	msleep(40);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->prepared)
		return 0;

	lcm_dcs_write_seq_static(ctx, 0x28);
	lcm_dcs_write_seq_static(ctx, 0x10);
	msleep(120);
	lcm_dcs_write_seq_static(ctx, 0x4F, 0x01);
	msleep(120);

	ctx->error = 0;
	ctx->prepared = false;
#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	lcm_panel_bias_disable();
#endif

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_info("%s\n", __func__);
	if (ctx->prepared)
		return 0;

#if defined(CONFIG_RT5081_PMU_DSV) || defined(CONFIG_MT6370_PMU_DSV)
	lcm_panel_bias_enable();
#endif

	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif
#ifdef PANEL_SUPPORT_READBACK
	lcm_panel_get_data(ctx);
#endif

	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

#define VAC (2400)
#define HAC (1080)

static struct drm_display_mode default_mode = {
	.clock = 382678,
	.hdisplay = HAC,
	.hsync_start = HAC + 165,//HFP
	.hsync_end = HAC + 165 + 22,//HSA
	.htotal = HAC + 165 + 22 + 22,//HBP1289
	.vdisplay = 2400,
	.vsync_start = VAC + 2528,//VFP
	.vsync_end = VAC + 2528 + 10,//VSA
	.vtotal = VAC + 2528 + 10 + 10,//VBP4948
	.vrefresh = 60,
};

static struct drm_display_mode performance_mode = {
	.clock = 382716,
	.hdisplay = HAC,
	.hsync_start = HAC + 165,//HFP
	.hsync_end = HAC + 165 + 22,//HSA
	.htotal = HAC + 165 + 22 + 22,//HBP
	.vdisplay = VAC,
	.vsync_start = VAC + 879,//VFP
	.vsync_end = VAC + 879 + 10,//VSA
	.vtotal = VAC + 879 + 10 + 10,//VBP3299
	.vrefresh = 90,
};

static struct drm_display_mode performance_mode1 = {
	.clock = 382678,
	.hdisplay = HAC,
	.hsync_start = HAC + 165,//HFP
	.hsync_end = HAC + 165 + 22,//HSA
	.htotal = HAC + 165 + 22 + 22,//HBP
	.vdisplay = VAC,
	.vsync_start = VAC + 54,//VFP
	.vsync_end = VAC + 54 + 10,//VSA
	.vtotal = VAC + 54 + 10 + 10,//VBP2474
	.vrefresh = 120,
};

#if defined(CONFIG_MTK_PANEL_EXT)
static struct mtk_panel_params ext_params = {
	.pll_clk = 588,
	.vfp_low_power = 4178,//45hz
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2400,
		.pic_width = 1080,
		.slice_height = 8,
		.slice_width = 540,
		.chunk_size = 540,
		.xmit_delay = 170,
		.dec_delay = 526,
		.scale_value = 32,
		.increment_interval = 43,
		.decrement_interval = 7,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 3511,
		.slice_bpg_offset = 3255,
		.initial_offset = 6144,
		.final_offset = 7072,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
		},
	.data_rate = 1176,
	.dyn_fps = {
		.switch_en = 1, .vact_timing_fps = 120,
	},
};

static struct mtk_panel_params ext_params_90hz = {
	.pll_clk = 588,
	.vfp_low_power = 2528,//60hz
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {

		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2400,
		.pic_width = 1080,
		.slice_height = 8,
		.slice_width = 540,
		.chunk_size = 540,
		.xmit_delay = 170,
		.dec_delay = 526,
		.scale_value = 32,
		.increment_interval = 43,
		.decrement_interval = 7,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 3511,
		.slice_bpg_offset = 3255,
		.initial_offset = 6144,
		.final_offset = 7072,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
		},
	.data_rate = 1176,
	.dyn_fps = {
		.switch_en = 1, .vact_timing_fps = 120,
	},
};

static struct mtk_panel_params ext_params_120hz = {
	.pll_clk = 588,
	.vfp_low_power = 2528,//idle 60hz
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x0A, .count = 1, .para_list[0] = 0x9C,
	},
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable = 1,
		.ver = 17,
		.slice_mode = 1,
		.rgb_swap = 0,
		.dsc_cfg = 34,
		.rct_on = 1,
		.bit_per_channel = 8,
		.dsc_line_buf_depth = 9,
		.bp_enable = 1,
		.bit_per_pixel = 128,
		.pic_height = 2400,
		.pic_width = 1080,
		.slice_height = 8,
		.slice_width = 540,
		.chunk_size = 540,
		.xmit_delay = 170,
		.dec_delay = 526,
		.scale_value = 32,
		.increment_interval = 43,
		.decrement_interval = 7,
		.line_bpg_offset = 12,
		.nfl_bpg_offset = 3511,
		.slice_bpg_offset = 3255,
		.initial_offset = 6144,
		.final_offset = 7072,
		.flatness_minqp = 3,
		.flatness_maxqp = 12,
		.rc_model_size = 8192,
		.rc_edge_factor = 6,
		.rc_quant_incr_limit0 = 11,
		.rc_quant_incr_limit1 = 11,
		.rc_tgt_offset_hi = 3,
		.rc_tgt_offset_lo = 3,
		},
	.data_rate = 1176,
	.dyn_fps = {
		.switch_en = 1, .vact_timing_fps = 120,
	},
};

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

static int panel_ata_check(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	unsigned char data[3];
	unsigned char id[3] = {0x00, 0x00, 0x00};
	ssize_t ret;

	pr_info("%s success\n", __func__);
#if 0
	ret = mipi_dsi_dcs_read(dsi, 0x4, data, 3);
	if (ret < 0)
		dev_info("%s error\n", __func__);

	DDPINFO("ATA read data %x %x %x\n", data[0], data[1], data[2]);

	if (data[0] == id[0] &&
			data[1] == id[1] &&
			data[2] == id[2])
		return 1;

	DDPINFO("ATA expect read data is %x %x %x\n",
			id[0], id[1], id[2]);
#endif
	return 1;
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
	void *handle, unsigned int level)
{
	char bl_tb0[] = {0x51, 0xFF};

	bl_tb0[1] = level;

	if (!cb)
		return -1;

	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));

	return 0;
}

struct drm_display_mode *get_mode_by_id(struct drm_panel *panel,
	unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &panel->connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}
static int mtk_panel_ext_param_set(struct drm_panel *panel, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(panel, mode);

	if (m->vrefresh == 60)
		ext->params = &ext_params;
	else if (m->vrefresh == 90)
		ext->params = &ext_params_90hz;
	else if (m->vrefresh == 120)
		ext->params = &ext_params_120hz;
	else
		ret = 1;

	return ret;
}


static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = panel_ata_check,
	.ext_param_set = mtk_panel_ext_param_set,
};
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};

static int lcm_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode2;
	struct drm_display_mode *mode3;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_info(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			default_mode.vrefresh);
		return -ENOMEM;
	}
	dev_info(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
				default_mode.hdisplay, default_mode.vdisplay,
				default_mode.vrefresh);

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(panel->connector, mode);

	mode2 = drm_mode_duplicate(panel->drm, &performance_mode);
	if (!mode2) {
		dev_info(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			performance_mode.hdisplay,
			performance_mode.vdisplay,
			performance_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode2);
	mode2->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(panel->connector, mode2);

	mode3 = drm_mode_duplicate(panel->drm, &performance_mode1);
	if (!mode3) {
		dev_info(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			performance_mode1.hdisplay,
			performance_mode1.vdisplay,
			performance_mode1.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode3);
	mode3->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(panel->connector, mode3);

	panel->connector->display_info.width_mm = 64;
	panel->connector->display_info.height_mm = 129;

	return 1;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};



static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lcm *ctx;
	struct device_node *backlight;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO
					| MIPI_DSI_MODE_VIDEO_SYNC_PULSE
					| MIPI_DSI_MODE_LPM
					| MIPI_DSI_MODE_EOT_PACKET
					| MIPI_DSI_CLOCK_NON_CONTINUOUS;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_info(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);
	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &lcm_drm_funcs;

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif
	pr_info("%s-\n", __func__);

	return ret;
}

static int lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "jdi,nt36672c,dphy,vdo", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-jdi-nt36672c-dphy-vdo",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("MEDIATEK");
MODULE_DESCRIPTION("jdi nt36672c VDO LCD Panel Driver");
MODULE_LICENSE("GPL v2");
