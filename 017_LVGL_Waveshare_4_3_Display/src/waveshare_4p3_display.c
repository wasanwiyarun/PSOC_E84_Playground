#define DT_DRV_COMPAT waveshare_ws43

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cy_graphics.h>
#include <cy_sysclk.h>

LOG_MODULE_REGISTER(waveshare_4p3, LOG_LEVEL_INF);

#define PANEL_ID_REG 0x80U
#define PANEL_CTRL_REG 0x85U
#define PANEL_POWER_REG 0x81U
#define PANEL_BRIGHTNESS_REG 0x86U
#define PANEL_ID_A 0xC3U
#define PANEL_ID_B 0xDEU
#define STRIDE_PIXELS 832U

struct waveshare_config {
	struct i2c_dt_spec i2c;
};

struct waveshare_data {
	cy_stc_gfx_context_t gfx_context;
	bool ready;
};

static cy_stc_gfx_layer_config_t graphics_layer = {
	.layer_type = GFX_LAYER_GRAPHICS,
	.input_format_type = vivRGB565,
	.tiling_type = vivLINEAR,
	.width = STRIDE_PIXELS,
	.height = 480U,
	.layer_enable = true,
	.visibility = true,
};

static cy_stc_gfx_layer_config_t overlay0_layer = { .layer_type = GFX_LAYER_OVERLAY0 };
static cy_stc_gfx_layer_config_t overlay1_layer = { .layer_type = GFX_LAYER_OVERLAY1 };
static cy_stc_gfx_dc_config_t dc_config = {
	.gfx_layer_config = &graphics_layer,
	.ovl0_layer_config = &overlay0_layer,
	.ovl1_layer_config = &overlay1_layer,
	.display_type = GFX_DISP_TYPE_DSI_DPI,
	.display_format = vivD24,
	.display_size = vivDISPLAY_CUSTOMIZED,
	.display_width = STRIDE_PIXELS,
	.display_height = 480U,
};
static cy_stc_gfx_gpu_cfg_t gpu_config = { .enable = false };
static cy_stc_mipidsi_display_params_t dsi_params = {
	.pixel_clock = 33768U,
	.hdisplay = STRIDE_PIXELS,
	.hsync_width = 10U,
	.hfp = 210U,
	.hbp = 20U,
	.vdisplay = 480U,
	.vsync_width = 5U,
	.vfp = 20U,
	.vbp = 20U,
};
static cy_stc_mipidsi_config_t dsi_config = {
	.num_of_lanes = 1U,
	.per_lane_mbps = 810U,
	.dpi_fmt = CY_MIPIDSI_FMT_RGB888,
	.dsi_mode = DSI_VIDEO_MODE,
	.max_phy_clk = 2500000000UL,
	.mode_flags = VID_MODE_TYPE_BURST | ENABLE_LOW_POWER_CMD | ENABLE_LOW_POWER,
	.display_params = &dsi_params,
};
static cy_stc_gfx_config_t gfx_config = {
	.dc_cfg = &dc_config,
	.gpu_cfg = &gpu_config,
	.mipi_dsi_cfg = &dsi_config,
	.display_update_type = GFX_DOUBLE_BUFFER,
	.clockHz = 399999999U,
};

static int panel_write(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t value)
{
	uint8_t data[] = { reg, value };
	return i2c_write_dt(i2c, data, sizeof(data));
}

static int panel_init(const struct i2c_dt_spec *i2c)
{
	uint8_t reg = PANEL_ID_REG;
	uint8_t id = 0U;
	int ret;

	k_msleep(100);
	ret = i2c_write_dt(i2c, &reg, sizeof(reg));
	if (ret != 0) return ret;
	ret = i2c_read_dt(i2c, &id, sizeof(id));
	if (ret != 0) return ret;
	if (id != PANEL_ID_A && id != PANEL_ID_B) return -ENODEV;

	ret = panel_write(i2c, PANEL_CTRL_REG, 0U);
	if (ret != 0) return ret;
	k_msleep(100);
	ret = panel_write(i2c, PANEL_CTRL_REG, 1U);
	if (ret != 0) return ret;
	k_msleep(100);
	ret = panel_write(i2c, PANEL_POWER_REG, 4U);
	if (ret != 0) return ret;
	k_msleep(100);
	return panel_write(i2c, PANEL_BRIGHTNESS_REG, 255U);
}

static int waveshare_write(const struct device *dev, uint16_t x, uint16_t y,
			   const struct display_buffer_descriptor *desc, const void *buf)
{
	struct waveshare_data *data = dev->data;

	if (!data->ready || x != 0U || y != 0U || desc->width != STRIDE_PIXELS || desc->height != 480U) {
		return -EINVAL;
	}
	return Cy_GFXSS_Set_FrameBuffer(GFXSS, (uint32_t *)buf, &data->gfx_context) == CY_GFX_SUCCESS ? 0 : -EIO;
}

static void waveshare_capabilities(const struct device *dev, struct display_capabilities *caps)
{
	ARG_UNUSED(dev);
	*caps = (struct display_capabilities) {
		.x_resolution = STRIDE_PIXELS,
		.y_resolution = 480U,
		.supported_pixel_formats = PIXEL_FORMAT_RGB_565,
		.current_pixel_format = PIXEL_FORMAT_RGB_565,
		.current_orientation = DISPLAY_ORIENTATION_NORMAL,
	};
}

static int waveshare_brightness(const struct device *dev, uint8_t brightness)
{
	const struct waveshare_config *config = dev->config;
	return panel_write(&config->i2c, PANEL_BRIGHTNESS_REG, (uint8_t)((brightness * 255U) / 100U));
}

static int waveshare_init(const struct device *dev)
{
	const struct waveshare_config *config = dev->config;
	struct waveshare_data *data = dev->data;
	cy_en_gfx_status_t status;
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) return -ENODEV;
	/*
	 * GFXSS is a separately clock-gated peripheral group on PSE84.  The
	 * generated ModusToolbox project enables these three slaves before the
	 * display stack touches their registers; Zephyr has no GFXSS clock
	 * binding yet, so do that equivalent setup in this custom driver.
	 */
	Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_GFXSS_GPU_PERI_NR,
				    CY_MMIO_GFXSS_GPU_GROUP_NR,
				    CY_MMIO_GFXSS_GPU_SLAVE_NR,
				    CY_MMIO_GFXSS_GPU_CLK_HF_NR);
	Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_GFXSS_DC_PERI_NR,
				    CY_MMIO_GFXSS_DC_GROUP_NR,
				    CY_MMIO_GFXSS_DC_SLAVE_NR,
				    CY_MMIO_GFXSS_DC_CLK_HF_NR);
	Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_GFXSS_MIPIDSI_PERI_NR,
				    CY_MMIO_GFXSS_MIPIDSI_GROUP_NR,
				    CY_MMIO_GFXSS_MIPIDSI_SLAVE_NR,
				    CY_MMIO_GFXSS_MIPIDSI_CLK_HF_NR);
	status = Cy_GFXSS_Init(GFXSS, &gfx_config, &data->gfx_context);
	if (status != CY_GFX_SUCCESS) return -EIO;
	ret = panel_init(&config->i2c);
	if (ret != 0) return ret;
	data->ready = true;
	LOG_INF("Waveshare 4.3 panel ready: 800x480, stride %u", STRIDE_PIXELS);
	return 0;
}

static const struct display_driver_api waveshare_api = {
	.write = waveshare_write,
	.get_capabilities = waveshare_capabilities,
	.set_brightness = waveshare_brightness,
};

static struct waveshare_data waveshare_data;
static const struct waveshare_config waveshare_config = {
	.i2c = I2C_DT_SPEC_INST_GET(0),
};

DEVICE_DT_INST_DEFINE(0, waveshare_init, NULL, &waveshare_data, &waveshare_config,
		      POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY, &waveshare_api);
