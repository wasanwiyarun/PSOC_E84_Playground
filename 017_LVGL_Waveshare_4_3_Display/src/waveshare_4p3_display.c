#define DT_DRV_COMPAT waveshare_ws43

#include <zephyr/device.h>
#include <zephyr/cache.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include "waveshare_4p3_display.h"

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
#define FRAMEBUFFER_PIXELS (STRIDE_PIXELS * 480U)

struct waveshare_config {
	struct i2c_dt_spec i2c;
};

struct waveshare_data {
	cy_stc_gfx_context_t gfx_context;
	bool ready;
	uint32_t flush_count;
	volatile uint32_t frame_count;
	volatile uint32_t dc_errors;
	uint32_t dsi_errors0;
	uint32_t dsi_errors1;
	const void *last_buffer;
	int last_error;
};

static struct waveshare_data waveshare_data;
static K_SEM_DEFINE(frame_done, 0, 2);

/*
 * The display controller raises this at the end of every scan-out.
 * Count actual frame completion, not reset/error interrupts, and wake the
 * thread waiting for a buffer swap. GCREGDISPLAYINTR is clear-on-read.
 */
static void waveshare_dc_irq_handler(const void *arg)
{
	ARG_UNUSED(arg);
	uint32_t status = GFXSS->GFXSS_DC.DCNANO.GCREGDISPLAYINTR;
	GFXSS->GFXSS_DC.MXDC.INTR = GFXSS->GFXSS_DC.MXDC.INTR;
	waveshare_data.dc_errors |= status & 0xA0001000U;
	if ((status & 1U) != 0U) {
		waveshare_data.frame_count++;
		k_sem_give(&frame_done);
	}
}

/*
 * A startup pattern is deliberately kept separate from the LVGL buffers.
 * It makes the DSI path observable before LVGL's first refresh and matches
 * the vendor example requirement that GFXSS starts with a valid buffer.
 */
static uint16_t startup_frame[FRAMEBUFFER_PIXELS]
	__attribute__((section("GFX_MEM"), aligned(128)));

static cy_stc_gfx_layer_config_t graphics_layer = {
	.layer_type = GFX_LAYER_GRAPHICS,
	.pos_x = 0,
	.pos_y = 0,
	.input_format_type = vivRGB565,
	.tiling_type = vivLINEAR,
	.width = STRIDE_PIXELS,
	.height = 480U,
	.zorder = 0,
	.layer_enable = true,
	.visibility = true,
};
static cy_stc_gfx_layer_config_t overlay0_layer = {
	.layer_type = GFX_LAYER_OVERLAY0,
	.input_format_type = vivRGB565,
	.tiling_type = vivLINEAR,
	.width = 1U,
	.height = 1U,
	.layer_enable = false,
	.visibility = false,
};
static cy_stc_gfx_layer_config_t overlay1_layer = {
	.layer_type = GFX_LAYER_OVERLAY1,
	.input_format_type = vivRGB565,
	.tiling_type = vivLINEAR,
	.width = 1U,
	.height = 1U,
	.layer_enable = false,
	.visibility = false,
};
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
static cy_stc_gfx_gpu_cfg_t gpu_config = { .enable = true };
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

static int panel_read(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t *value)
{
	int ret = i2c_write_dt(i2c, &reg, sizeof(reg));
	if (ret != 0) return ret;
	return i2c_read_dt(i2c, value, sizeof(*value));
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
	printk("OK DISPLAY MODULE_I2C_OK panel_id=0x%02x\n", id);

	ret = panel_write(i2c, PANEL_CTRL_REG, 0U);
	if (ret != 0) return ret;
	k_msleep(100);
	ret = panel_write(i2c, PANEL_CTRL_REG, 1U);
	if (ret != 0) return ret;
	k_msleep(100);
	ret = panel_write(i2c, PANEL_POWER_REG, 4U);
	if (ret != 0) return ret;
	k_msleep(100);
	ret = panel_write(i2c, PANEL_BRIGHTNESS_REG, 255U);
	if (ret != 0) return ret;
	k_msleep(100);
	return 0;
}

static int present_frame(struct waveshare_data *data, const void *buf)
{
	int ret = sys_cache_data_flush_range((void *)buf, sizeof(startup_frame));
	if (ret != 0) return ret;
	unsigned int key = irq_lock();
	k_sem_reset(&frame_done);
	Cy_GFXSS_Clear_DC_Interrupt(GFXSS, &data->gfx_context);
	cy_en_gfx_status_t result = Cy_GFXSS_Set_FrameBuffer(
		GFXSS, (uint32_t *)buf, &data->gfx_context);
	irq_unlock(key);
	if (result != CY_GFX_SUCCESS) return -EIO;
	/* The first boundary latches the new address; wait another frame before
	 * LVGL can reuse the old buffer. Both waits have a bounded timeout.
	 */
	for (int i = 0; i < 2; i++) {
		if (k_sem_take(&frame_done, K_MSEC(100)) != 0) return -ETIMEDOUT;
	}
	if (GFXSS->GFXSS_DC.DCNANO.GCREGFRAMEBUFFERADDRESS != (uint32_t)buf) {
		return -EIO;
	}
	data->last_buffer = buf;
	return 0;
}

static void startup_frame_fill(void)
{
	for (uint32_t y = 0U; y < 480U; ++y) {
		for (uint32_t x = 0U; x < STRIDE_PIXELS; ++x) {
			/* Four highly visible RGB565 bars, with a white border. */
			uint16_t color = (x < 208U) ? 0xF800U :
					 (x < 416U) ? 0x07E0U :
					 (x < 624U) ? 0x001FU : 0xFFE0U;
			if (x < 8U || x >= (STRIDE_PIXELS - 8U) || y < 8U || y >= 472U) {
				color = 0xFFFFU;
			}
			startup_frame[y * STRIDE_PIXELS + x] = color;
		}
	}
}

static int waveshare_write(const struct device *dev, uint16_t x, uint16_t y,
			   const struct display_buffer_descriptor *desc, const void *buf)
{
	struct waveshare_data *data = dev->data;

	if (!data->ready || buf == NULL || x != 0U || y != 0U ||
	    desc->width != STRIDE_PIXELS || desc->height != 480U ||
	    desc->pitch != STRIDE_PIXELS || desc->buf_size < sizeof(startup_frame) ||
	    ((uintptr_t)buf % 128U) != 0U) {
		printk("FAIL DISPLAY LVGL_WRITE x=%u y=%u width=%u height=%u\n",
		       x, y, desc->width, desc->height);
		data->last_error = -EINVAL;
		return data->last_error;
	}
	data->last_error = present_frame(data, buf);
	if (data->last_error != 0) {
		printk("FAIL DISPLAY PRESENT ret=%d\n", data->last_error);
		return data->last_error;
	}
	data->flush_count++;
	if (data->flush_count == 1U) {
		printk("OK DISPLAY LVGL_FLUSH=1\n");
	}
	return 0;
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
	/* Zephyr's brightness API is 0..255, not a percentage. */
	return panel_write(&config->i2c, PANEL_BRIGHTNESS_REG, brightness);
}

static int waveshare_blanking_off(const struct device *dev)
{
	return waveshare_brightness(dev, UINT8_MAX);
}

static int waveshare_blanking_on(const struct device *dev)
{
	return waveshare_brightness(dev, 0);
}

int waveshare_get_status(const struct device *dev, struct waveshare_status *status)
{
	struct waveshare_data *data = dev->data;
	const struct waveshare_config *config = dev->config;
	*status = (struct waveshare_status){0};
	int ret = panel_read(&config->i2c, PANEL_ID_REG, &status->panel_id);
	if (ret != 0) return ret;
	data->dsi_errors0 |= GFXSS->GFXSS_MIPIDSI.DWCMIPIDSI.INT_ST0;
	data->dsi_errors1 |= GFXSS->GFXSS_MIPIDSI.DWCMIPIDSI.INT_ST1;
	status->frames = data->frame_count;
	status->flushes = data->flush_count;
	status->dc_errors = data->dc_errors;
	status->dsi_errors0 = data->dsi_errors0;
	status->dsi_errors1 = data->dsi_errors1;
	status->phy = GFXSS->GFXSS_MIPIDSI.DWCMIPIDSI.PHY_STATUS;
	status->clock_hz = Cy_SysClk_ClkHfGetFrequency(CY_MMIO_GFXSS_DC_CLK_HF_NR) /
		(Cy_SysClk_PeriGroupGetDivider(PERI_1_GROUP_3) + 1U);
	status->framebuffer = GFXSS->GFXSS_DC.DCNANO.GCREGFRAMEBUFFERADDRESS;
	status->last_error = data->last_error;
	if (data->last_buffer != NULL) {
		status->crc = crc32_ieee(data->last_buffer, sizeof(startup_frame));
	}
	if (!data->ready || data->last_error != 0 || status->frames < 2 ||
	    status->flushes == 0 || status->dc_errors != 0 ||
	    status->dsi_errors0 != 0 || status->dsi_errors1 != 0 ||
	    (status->phy & 1U) == 0 ||
	    (status->clock_hz != 99999999U && status->clock_hz != 100000000U) ||
	    status->framebuffer != (uint32_t)data->last_buffer ||
	    (status->panel_id != PANEL_ID_A && status->panel_id != PANEL_ID_B)) {
		return -EIO;
	}
	return 0;
}

static int waveshare_init(const struct device *dev)
{
	const struct waveshare_config *config = dev->config;
	struct waveshare_data *data = dev->data;
	cy_en_gfx_status_t status;
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) return -ENODEV;
	/* GFXSS uses the 400 MHz HF1 parent, but its peripheral/DSI reference
	 * clock must be 100 MHz, as in the vendor BSP's peripheral clocks.
	 * PeriGroupSlaveInit only releases reset/enables the IP; it does not
	 * configure this divider (the register stores divisor minus one).
	 */
	if (Cy_SysClk_PeriGroupSetDivider(PERI_1_GROUP_3, 3U) != CY_SYSCLK_SUCCESS) {
		return -EIO;
	}
	printk("INFO DISPLAY CLOCK dc=%u dsi=%u gpu=%u\n",
	       Cy_SysClk_ClkHfGetFrequency(CY_MMIO_GFXSS_DC_CLK_HF_NR),
	       Cy_SysClk_ClkHfGetFrequency(CY_MMIO_GFXSS_MIPIDSI_CLK_HF_NR),
	       Cy_SysClk_ClkHfGetFrequency(CY_MMIO_GFXSS_GPU_CLK_HF_NR));
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
	startup_frame_fill();
	sys_cache_data_flush_range(startup_frame, sizeof(startup_frame));
	graphics_layer.buffer_address = (gctADDRESS *)startup_frame;
	graphics_layer.uv_buffer_address = (gctADDRESS *)startup_frame;
	status = Cy_GFXSS_Init(GFXSS, &gfx_config, &data->gfx_context);
	if (status != CY_GFX_SUCCESS) return -EIO;
	Cy_GFXSS_Clear_DC_Interrupt(GFXSS, &data->gfx_context);
	IRQ_CONNECT(gfxss_interrupt_dc_IRQn, 3, waveshare_dc_irq_handler, NULL, 0);
	irq_enable(gfxss_interrupt_dc_IRQn);
	printk("OK DISPLAY MODULE_GFXSS_OK startup_pattern=RGBY\n");
	ret = panel_init(&config->i2c);
	if (ret != 0) return ret;
	ret = present_frame(data, startup_frame);
	if (ret != 0) {
		printk("FAIL DISPLAY STARTUP_FRAME ret=%d\n", ret);
		return ret;
	}
	/* Discard startup status while the video pipeline was being enabled.
	 * All later errors are retained in the diagnostic status.
	 */
	(void)GFXSS->GFXSS_MIPIDSI.DWCMIPIDSI.INT_ST0;
	(void)GFXSS->GFXSS_MIPIDSI.DWCMIPIDSI.INT_ST1;
	data->ready = true;
	k_msleep(100);
	printk("INFO DISPLAY SCANOUT frames=%u phy=0x%x dsi_errors=0x%x,0x%x\n",
	       data->frame_count, GFXSS->GFXSS_MIPIDSI.DWCMIPIDSI.PHY_STATUS,
	       GFXSS->GFXSS_MIPIDSI.DWCMIPIDSI.INT_ST0,
	       GFXSS->GFXSS_MIPIDSI.DWCMIPIDSI.INT_ST1);
	printk("OK DISPLAY MODULE_READY width=800 height=480 stride=%u\n", STRIDE_PIXELS);
	LOG_INF("Waveshare 4.3 panel ready: 800x480, stride %u", STRIDE_PIXELS);
	return 0;
}

static DEVICE_API(display, waveshare_api) = {
	.write = waveshare_write,
	.get_capabilities = waveshare_capabilities,
	.set_brightness = waveshare_brightness,
	.blanking_on = waveshare_blanking_on,
	.blanking_off = waveshare_blanking_off,
};

static const struct waveshare_config waveshare_config = {
	.i2c = I2C_DT_SPEC_INST_GET(0),
};

DEVICE_DT_INST_DEFINE(0, waveshare_init, NULL, &waveshare_data, &waveshare_config,
		      POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY, &waveshare_api);
