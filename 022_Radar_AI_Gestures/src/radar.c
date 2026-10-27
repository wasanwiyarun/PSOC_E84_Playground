/* Chapter-local Zephyr/PDL port. Board wiring follows Temp/Radar's AI BSP.
 * SCB3 is not enabled as a Zephyr device; this module exclusively owns it.
 * Bounded polling keeps register and 12-bit FIFO phases under one manual CS.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <cy_pdl.h>
#include <errno.h>
#include <string.h>
#include "radar.h"
#include "xensiv_bgt60trxx.h"
#include "xensiv_bgt60trxx_platform.h"

static xensiv_bgt60trxx_t radar;
static cy_stc_scb_spi_context_t context;
static bool ready;
static bool streaming;
static uint32_t spi_hz;
static int iface;
/* P0 numerical register settings from Temp/Radar/proj_cm55/radar_settings.h:
 * 61.020099..61.479903 GHz, RX1, 128 samples/chirp, 1 chirp/frame,
 * 2.352941 MS/s, 5.00407 ms/frame. Continuous FIFO port derived from 021. */
static const uint32_t settings[]={
    0x011e8270,0x03088210,0x09e967fd,0x0b0805b4,0x0d1027ff,0x0f010700,
    0x11000000,0x13000000,0x15000000,0x17000be0,0x19000000,0x1b000000,
    0x1d000000,0x1f000b60,0x21103c51,0x231ff41f,0x25006f7b,0x2d000490,
    0x3b000480,0x49000480,0x57000480,0x5911be0e,0x5b44c40a,0x5d000000,
    0x5f787e1e,0x61f5208a,0x630000a4,0x65000252,0x67000080,0x69000000,
    0x6b000000,0x6d000000,0x6f093910,0x7f000100,0x8f000100,0x9f000100,
    0xad000000,0xb7000000
};

void xensiv_bgt60trxx_platform_rst_set(const void *p,bool high)
{ ARG_UNUSED(p); Cy_GPIO_Write(GPIO_PRT20,7,high); }
void xensiv_bgt60trxx_platform_spi_cs_set(const void *p,bool high)
{ ARG_UNUSED(p); Cy_GPIO_Write(GPIO_PRT21,7,high); }
void xensiv_bgt60trxx_platform_delay(uint32_t ms) { k_msleep(ms); }
uint32_t xensiv_bgt60trxx_platform_word_reverse(uint32_t word) { return sys_cpu_to_be32(word); }
void xensiv_bgt60trxx_platform_assert(bool expr) { __ASSERT_NO_MSG(expr); }

static int transfer(const uint8_t *tx,uint8_t *rx,uint16_t *fifo,uint32_t count,unsigned bits)
{
    CY_REG32_CLR_SET(SCB_TX_CTRL(SCB3),SCB_TX_CTRL_DATA_WIDTH,bits-1U);
    CY_REG32_CLR_SET(SCB_RX_CTRL(SCB3),SCB_RX_CTRL_DATA_WIDTH,bits-1U);
    Cy_SCB_SetByteMode(SCB3,bits==8);
    Cy_SCB_SPI_ClearRxFifo(SCB3);
    int64_t deadline=k_uptime_get()+20;
    for(uint32_t i=0;i<count;i++) {
        Cy_SCB_SPI_Write(SCB3,tx?tx[i]:0xffffU);
        while(!Cy_SCB_SPI_GetNumInRxFifo(SCB3)) {
            if(k_uptime_get()>=deadline) goto timeout;
        }
        uint32_t v=Cy_SCB_SPI_Read(SCB3);
        if(rx) rx[i]=(uint8_t)v;
        if(fifo) fifo[i]=(uint16_t)v;
    }
    while(Cy_SCB_SPI_IsBusBusy(SCB3)) if(k_uptime_get()>=deadline) goto timeout;
    return XENSIV_BGT60TRXX_STATUS_OK;
timeout:
    Cy_GPIO_Write(GPIO_PRT21,7,1);
    Cy_SCB_SPI_Disable(SCB3,&context);
    Cy_SCB_SPI_ClearTxFifo(SCB3); Cy_SCB_SPI_ClearRxFifo(SCB3);
    Cy_SCB_SPI_Enable(SCB3); ready=false;
    return XENSIV_BGT60TRXX_STATUS_COM_ERROR;
}
int32_t xensiv_bgt60trxx_platform_spi_transfer(void *p,uint8_t *tx,uint8_t *rx,uint32_t len)
{ ARG_UNUSED(p); return transfer(tx,rx,NULL,len,8); }
int32_t xensiv_bgt60trxx_platform_spi_fifo_read(void *p,uint16_t *rx,uint32_t len)
{ ARG_UNUSED(p); return transfer(NULL,NULL,rx,len,12); }

uint32_t radar_spi_hz(void) { return spi_hz; }
int radar_id(uint32_t *id)
{ return ready?xensiv_bgt60trxx_get_reg(&radar,XENSIV_BGT60TRXX_REG_CHIP_ID,id):-ENODEV; }

int radar_init(void)
{
    ready=false;
    streaming=false;
    printk("RADAR INIT stage=pins\n");
    Cy_GPIO_Pin_FastInit(GPIO_PRT21,7,CY_GPIO_DM_STRONG_IN_OFF,1,HSIOM_SEL_GPIO);
    Cy_GPIO_Pin_FastInit(GPIO_PRT20,7,CY_GPIO_DM_STRONG_IN_OFF,1,HSIOM_SEL_GPIO);
    Cy_GPIO_Pin_FastInit(GPIO_PRT20,3,CY_GPIO_DM_PULLDOWN,0,HSIOM_SEL_GPIO);
    Cy_GPIO_Pin_FastInit(GPIO_PRT21,4,CY_GPIO_DM_HIGHZ,1,P21_4_SCB3_SPI_MISO);
    Cy_GPIO_Pin_FastInit(GPIO_PRT21,5,CY_GPIO_DM_STRONG_IN_OFF,1,P21_5_SCB3_SPI_MOSI);
    Cy_GPIO_Pin_FastInit(GPIO_PRT21,6,CY_GPIO_DM_STRONG_IN_OFF,0,P21_6_SCB3_SPI_CLK);
    Cy_GPIO_SetDriveSel(GPIO_PRT21,5,CY_GPIO_DRIVE_1_8);
    Cy_GPIO_SetDriveSel(GPIO_PRT21,6,CY_GPIO_DRIVE_1_8);
    Cy_SCB_SPI_Disable(SCB3,&context);
    printk("RADAR INIT stage=clock\n");
    /* Same valid divider index as the reference BSP (group has indices 0..4). */
    if(Cy_SysClk_PeriPclkDisableDivider(PCLK_SCB3_CLOCK_SCB_EN,CY_SYSCLK_DIV_8_BIT,1)!=CY_SYSCLK_SUCCESS ||
       Cy_SysClk_PeriPclkSetDivider(PCLK_SCB3_CLOCK_SCB_EN,CY_SYSCLK_DIV_8_BIT,1,3)!=CY_SYSCLK_SUCCESS ||
       Cy_SysClk_PeriPclkAssignDivider(PCLK_SCB3_CLOCK_SCB_EN,CY_SYSCLK_DIV_8_BIT,1)!=CY_SYSCLK_SUCCESS ||
       Cy_SysClk_PeriPclkEnableDivider(PCLK_SCB3_CLOCK_SCB_EN,CY_SYSCLK_DIV_8_BIT,1)!=CY_SYSCLK_SUCCESS) return -EIO;
    spi_hz=Cy_SysClk_PeriPclkGetFrequency(PCLK_SCB3_CLOCK_SCB_EN,CY_SYSCLK_DIV_8_BIT,1)/4;
    printk("RADAR INIT stage=spi hz=%u\n",spi_hz);
    const cy_stc_scb_spi_config_t config={
        .spiMode=CY_SCB_SPI_MASTER,.subMode=CY_SCB_SPI_MOTOROLA,
        .sclkMode=CY_SCB_SPI_CPHA0_CPOL0,.oversample=4,
        .rxDataWidth=8,.txDataWidth=8,.enableMsbFirst=true,
        .enableMisoLateSample=true
    };
    if(Cy_SCB_SPI_Init(SCB3,&config,&context)!=CY_SCB_SPI_SUCCESS) return -EIO;
    Cy_SCB_SPI_SetActiveSlaveSelect(SCB3,CY_SCB_SPI_SLAVE_SELECT1);
    Cy_SCB_SPI_Enable(SCB3);
    radar.iface=&iface;
    printk("RADAR INIT stage=reset\n");
    xensiv_bgt60trxx_hard_reset(&radar);
    int ret=xensiv_bgt60trxx_init(&radar,&iface,false);
    printk("RADAR INIT stage=identity ret=%d\n",ret);
    if(!ret && xensiv_bgt60trxx_get_device(&radar)!=XENSIV_DEVICE_BGT60TR13C) ret=-ENODEV;
    if(!ret) ret=xensiv_bgt60trxx_config(&radar,settings,ARRAY_SIZE(settings));
    if(!ret) ret=xensiv_bgt60trxx_set_fifo_limit(&radar,RADAR_SAMPLES);
    ready=ret==0;
    return ret;
}

static int stream_error(int ret)
{
    /* Even if SPI is broken, hardware reset stops the RF transmitter. */
    xensiv_bgt60trxx_hard_reset(&radar);
    ready=false;
    streaming=false;
    return ret;
}

int radar_stream_start(void)
{
    if(!ready) return -ENODEV;
    if(streaming) return -EBUSY;
    int ret=xensiv_bgt60trxx_start_frame(&radar,false);
    if(!ret) ret=xensiv_bgt60trxx_soft_reset(&radar,XENSIV_BGT60TRXX_RESET_FIFO);
    if(!ret) ret=xensiv_bgt60trxx_enable_data_test_mode(&radar,false);
    if(!ret) ret=xensiv_bgt60trxx_start_frame(&radar,true);
    if(ret) return stream_error(ret);
    streaming=true;
    return 0;
}

int radar_stream_stop(void)
{
    if(!streaming) return 0;
    int ret=xensiv_bgt60trxx_start_frame(&radar,false);
    if(ret) return stream_error(ret);
    streaming=false;
    return 0;
}

int radar_stream_read(unsigned frames,uint16_t *samples,struct radar_result *out)
{
    memset(out,0,sizeof(*out));
    if(!ready || !streaming) return out->ret=-ENODEV;
    if(!frames || frames>RADAR_MAX_FRAMES || !samples) return out->ret=-EINVAL;
    int ret=0;
    uint32_t begin=k_cycle_get_32(); int64_t deadline=k_uptime_get()+2000;
    while(!ret && out->frames<frames) {
        ret=xensiv_bgt60trxx_get_reg(&radar,XENSIV_BGT60TRXX_REG_FSTAT_TR13C,&out->fstat);
        if(ret) break;
        if(out->fstat & (XENSIV_BGT60TRXX_REG_FSTAT_FOF_ERR_MSK |
                        XENSIV_BGT60TRXX_REG_FSTAT_FUF_ERR_MSK |
                        XENSIV_BGT60TRXX_REG_FSTAT_CLK_NUM_ERR_MSK |
                        XENSIV_BGT60TRXX_REG_FSTAT_SPI_BURST_ERR_MSK)) { ret=-EIO; break; }
        if(Cy_GPIO_Read(GPIO_PRT20,3)) out->irq_observed++;
        if((out->fstat & XENSIV_BGT60TRXX_REG_FSTAT_FILL_STATUS_MSK)>=RADAR_SAMPLES/2) {
            uint16_t *dst=samples+out->samples;
            ret=xensiv_bgt60trxx_get_fifo_data(&radar,dst,RADAR_SAMPLES);
            if(ret) break;
            out->frames++; out->samples+=RADAR_SAMPLES;
        } else k_msleep(1);
        if(k_uptime_get()>=deadline) { ret=-ETIMEDOUT; break; }
    }
    out->elapsed_us=k_cyc_to_us_floor32(k_cycle_get_32()-begin);
    if(ret) stream_error(ret);
    out->ret=ret; return ret;
}
