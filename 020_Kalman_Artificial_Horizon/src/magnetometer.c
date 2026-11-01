/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <cy_pdl.h>
#include <math.h>
#include <string.h>
#include "bmm350.h"
#include "magnetometer.h"

/* Board-default routing, 1.8 V: P3.0=SCL, P3.1=SDA; P6.4=MAG_INT.
 * Dedicated PDL backend until Zephyr has an Infineon I3C controller driver.
 * Poll the PDL interrupt handler with NVIC disabled: a single worker owns
 * the controller, and every asynchronous transfer has a deadline.
 */
static cy_stc_i3c_context_t bus;
static cy_stc_i3c_device_t target;
static struct bmm350_dev sensor;
static struct magnetometer_status state;
static int64_t operation_deadline;

static int transfer_poll(uint8_t *data, uint32_t size, bool read)
{
    if (k_uptime_get() >= operation_deadline) return -ETIMEDOUT;
    cy_stc_i3c_controller_xfer_config_t xfer = {
        .targetAddress=target.dynamicAddress, .buffer=data,
        .bufferSize=size, .toc=true,
    };
    cy_en_i3c_status_t ret = read ? Cy_I3C_ControllerRead(I3C_CORE,&xfer,&bus) :
                                  Cy_I3C_ControllerWrite(I3C_CORE,&xfer,&bus);
    if (ret != CY_I3C_SUCCESS) return -EIO;
    int64_t deadline=k_uptime_get()+20;
    while (true) {
        Cy_I3C_Interrupt(I3C_CORE,&bus);
        state.bus_status=Cy_I3C_GetBusStatus(I3C_CORE,&bus);
        if (!(state.bus_status & CY_I3C_CONTROLLER_BUSY)) {
            uint32_t expected=read?CY_I3C_CONTROLLER_I3C_SDR_RD_XFER:
                                   CY_I3C_CONTROLLER_I3C_SDR_WR_XFER;
            if (state.bus_status == expected) { state.transfers++; return 0; }
            break;
        }
        /* Service completion once after any preemption before evaluating
         * the deadline; elapsed wall time alone is not a hardware error. */
        if(k_uptime_get()>=deadline) break;
        k_busy_wait(10);
    }
    printk("WARN COMPASS TRANSFER read=%u size=%u bus=%08x\n",read,size,state.bus_status);
    Cy_I3C_ControllerAbortTransfer(I3C_CORE,&bus);
    Cy_I3C_Resume(I3C_CORE,&bus);
    return -EIO;
}

static int transfer(uint8_t *data, uint32_t size, bool read)
{
    /* Only the bounded polled transfer outranks graphics, not the long PDL
     * CCC/init delays. Otherwise a full-frame refresh can consume its entire
     * 20 ms budget before FIFO/completion service runs. IMU remains higher. */
    int priority=k_thread_priority_get(k_current_get());
    k_thread_priority_set(k_current_get(),3);
    int ret=transfer_poll(data,size,read);
    k_thread_priority_set(k_current_get(),priority);
    return ret;
}

static int8_t read_reg(uint8_t reg, uint8_t *data, uint32_t size, void *ctx)
{
    ARG_UNUSED(ctx);
    if (transfer(&reg,1,false)) return -1;
    return transfer(data,size,true)?-1:0;
}
static int8_t write_reg(uint8_t reg, const uint8_t *data, uint32_t size, void *ctx)
{
    ARG_UNUSED(ctx);
    uint8_t buffer[32];
    if (size>sizeof(buffer)-1) return -1;
    buffer[0]=reg; memcpy(buffer+1,data,size);
    return transfer(buffer,size+1,false)?-1:0;
}
static void delay_us(uint32_t us, void *ctx)
{
    ARG_UNUSED(ctx);
    if(us>=1000) k_usleep(us); else k_busy_wait(us);
}

int magnetometer_init(void)
{
    memset(&state,0,sizeof(state));
    state.ret=-ENODEV;
    printk("INFO COMPASS INIT stage=clock\n");
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_I3C_PERI_NR,CY_MMIO_I3C_GROUP_NR,
                               CY_MMIO_I3C_SLAVE_NR,CY_MMIO_I3C_CLK_HF_NR);
    /* Dedicated I3C peripheral clock, not an SCB divider. */
    Cy_SysClk_PeriPclkDisableDivider(PCLK_I3C_CLOCK_I3C_EN,CY_SYSCLK_DIV_8_BIT,0);
    Cy_SysClk_PeriPclkSetDivider(PCLK_I3C_CLOCK_I3C_EN,CY_SYSCLK_DIV_8_BIT,0,0);
    Cy_SysClk_PeriPclkAssignDivider(PCLK_I3C_CLOCK_I3C_EN,CY_SYSCLK_DIV_8_BIT,0);
    Cy_SysClk_PeriPclkEnableDivider(PCLK_I3C_CLOCK_I3C_EN,CY_SYSCLK_DIV_8_BIT,0);
    state.clock_hz=Cy_SysClk_PeriPclkGetFrequency(PCLK_I3C_CLOCK_I3C_EN,CY_SYSCLK_DIV_8_BIT,0);
    printk("INFO COMPASS INIT clock_hz=%u\n",state.clock_hz);
    if(state.clock_hz!=100000000) { state.ret=-EINVAL; return state.ret; }
    cy_stc_gpio_pin_config_t pin={
        .outVal=1,.driveMode=CY_GPIO_DM_CFGOUT3_STRONG_PULLUP_HIGHZ,
        .hsiom=P3_0_I3C_I3C_SCL,.vtrip=CY_GPIO_VTRIP_CMOS,
        .slewRate=CY_GPIO_SLEW_FAST,.driveSel=CY_GPIO_DRIVE_1_2,
        .pullUpRes=CY_GPIO_PULLUP_RES_1800,.nonSec=1,
    };
    Cy_GPIO_Pin_Init(GPIO_PRT3,0,&pin);
    pin.hsiom=P3_1_I3C_I3C_SDA;
    Cy_GPIO_Pin_Init(GPIO_PRT3,1,&pin);
    /* No external INT needed for this first version: data ready is polled. */
    cy_stc_i3c_config_t config={
        .i3cMode=CY_I3C_CONTROLLER,.i3cBusMode=CY_I3C_BUS_PURE,
        .manualDataRate=true,.i3cClockHz=100000000,.i3cSclRate=10000000,
        .openDrainSclRate=2500000,.dynamicAddr=8,.cmdQueueEmptyThld=1,
        .ibiDataThld=15,.sdaHoldTime=1,.busFreeTime=32,
        /* 200 ns OD high satisfies BMM350's 160 ns minimum. */
        .openDrainHighCnt=20,.openDrainLowCnt=20,
        .pushPullHighCnt=5,.pushPullLowCnt=5,
        .txEmptyBufThld=CY_I3C_1_WORD_DEPTH,.rxBufThld=CY_I3C_1_WORD_DEPTH,
        .txBufStartThld=CY_I3C_1_WORD_DEPTH,.rxBufStartThld=CY_I3C_1_WORD_DEPTH,
    };
    irq_disable(i3c_interrupt_IRQn);
    cy_en_i3c_status_t ret=Cy_I3C_Init(I3C_CORE,&config,&bus);
    if(ret) goto fail;
    Cy_I3C_Enable(I3C_CORE,&bus);
    cy_stc_i3c_ccc_payload_t payload={0};
    cy_stc_i3c_ccc_cmd_t ccc={.address=CY_I3C_BROADCAST_ADDR,
        .cmd=CY_I3C_CCC_RSTDAA(true),.data=&payload};
    ret=Cy_I3C_SendCCCCmd(I3C_CORE,&ccc,&bus);
    printk("INFO COMPASS INIT stage=rstdaa pdl=%08x\n",ret);
    if(ret) goto fail;
    target=(cy_stc_i3c_device_t){.staticAddress=0x15};
    ret=Cy_I3C_ControllerAttachI3CDevice(I3C_CORE,&target,&bus);
    printk("INFO COMPASS INIT stage=setdasa pdl=%08x address=%02x\n",ret,target.dynamicAddress);
    if(ret) goto fail;
    state.address=target.dynamicAddress;
    sensor=(struct bmm350_dev){.read=read_reg,.write=write_reg,.delay_us=delay_us};
    operation_deadline=k_uptime_get()+3000;
    /* A warm MCU reset leaves the sensor running with its OTP powered off.
     * Reset it explicitly, THEN restore I3C addressing before Bosch init.
     * Merely skipping the API's reset only works on a fresh power-on. */
    uint8_t reset=BMM350_CMD_SOFTRESET;
    if(bmm350_set_regs(BMM350_REG_CMD,&reset,1,&sensor)) goto fail;
    k_msleep(30);
    Cy_I3C_DeInit(I3C_CORE,&bus);
    ret=Cy_I3C_Init(I3C_CORE,&config,&bus);
    if(ret) goto fail;
    Cy_I3C_Enable(I3C_CORE,&bus);
    ret=Cy_I3C_SendCCCCmd(I3C_CORE,&ccc,&bus);
    if(ret) goto fail;
    target=(cy_stc_i3c_device_t){.staticAddress=0x15};
    ret=Cy_I3C_ControllerAttachI3CDevice(I3C_CORE,&target,&bus);
    printk("INFO COMPASS INIT stage=reset_readdress pdl=%08x address=%02x\n",ret,target.dynamicAddress);
    if(ret) goto fail;
    state.address=target.dynamicAddress;
    operation_deadline=k_uptime_get()+3000;
    int result=bmm350_init(&sensor);
    state.chip_id=sensor.chip_id;
    printk("INFO COMPASS INIT stage=sensor ret=%d chip_id=%02x bus=%08x\n",result,state.chip_id,state.bus_status);
    if(!result) result=bmm350_configure_interrupt(BMM350_PULSED,BMM350_ACTIVE_HIGH,
                   BMM350_INTR_PUSH_PULL,BMM350_UNMAP_FROM_PIN,&sensor);
    if(!result) result=bmm350_enable_interrupt(BMM350_ENABLE_INTERRUPT,&sensor);
    if(!result) result=bmm350_set_odr_performance(BMM350_DATA_RATE_25HZ,BMM350_AVERAGING_8,&sensor);
    if(!result) result=bmm350_enable_axes(BMM350_X_EN,BMM350_Y_EN,BMM350_Z_EN,&sensor);
    if(!result) result=bmm350_set_powermode(BMM350_NORMAL_MODE,&sensor);
    state.ret=result? -EIO:0; state.ready=!result;
    printk("%s COMPASS INIT chip_id=%02x address=%02x ret=%d\n",result?"FAIL":"OK",
           state.chip_id,state.address,state.ret);
    return state.ret;
fail:
    state.ret=-EIO;
    printk("FAIL COMPASS INIT pdl=%08x\n",ret);
    return state.ret;
}

int magnetometer_read(void)
{
    if(!state.ready) return state.ret;
    operation_deadline=k_uptime_get()+100;
    uint8_t ready=0;
    int ret=bmm350_get_regs(BMM350_REG_INT_STATUS,&ready,1,&sensor);
    if(!ret && !(ready & BMM350_DRDY_DATA_REG_MSK)) return -EAGAIN;
    struct bmm350_mag_temp_data data;
    if(!ret) ret=bmm350_get_compensated_mag_xyz_temp_data(&data,&sensor);
    if(!ret && isfinite(data.x) && isfinite(data.y) && isfinite(data.z) && isfinite(data.temperature)) {
        state.xyz[0]=data.x; state.xyz[1]=data.y; state.xyz[2]=data.z;
        /* Application warning only, not a calibration or a sensor fault. */
        state.field_high=data.x*data.x+data.y*data.y+data.z*data.z > 100.0f*100.0f;
        state.temperature=data.temperature; state.samples++; state.sample_ms=k_uptime_get();
        state.ret=0;
    } else {
        state.errors++; state.ret=-EIO;
        /* Fail closed after a transport error; do not reuse uncertain bus
         * state or pending buffer pointers. A board reset reinitializes it. */
        state.ready=false;
    }
    return state.ret;
}
void magnetometer_status(struct magnetometer_status *out) { *out=state; }
