/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <cy_pdl.h>

/* A debugger reset can interrupt an SCB0 read while a powered sensor holds
 * SDA low. Clear only that bus before Zephyr initializes its controller and
 * sensors. Never drive either line high; external pull-ups establish voltage.
 * P3 I3C and P17 display/touch buses are not touched.
 */
BUILD_ASSERT(CONFIG_I2C_INIT_PRIORITY > 49);
BUILD_ASSERT(CONFIG_SENSOR_INIT_PRIORITY > CONFIG_I2C_INIT_PRIORITY);

static bool wait_scl(void)
{
    for (int n=0;n<100;n++) {
        if (Cy_GPIO_Read(GPIO_PRT8,0)) return true;
        k_busy_wait(10);
    }
    return false;
}

static int sensor_bus_clear(void)
{
    en_hsiom_sel_t mux[2]; uint32_t mode[2], out[2];
    for (int pin=0;pin<2;pin++) {
        mux[pin]=Cy_GPIO_GetHSIOM(GPIO_PRT8,pin);
        mode[pin]=Cy_GPIO_GetDrivemode(GPIO_PRT8,pin);
        out[pin]=Cy_GPIO_ReadOut(GPIO_PRT8,pin);
        Cy_GPIO_Pin_FastInit(GPIO_PRT8,pin,CY_GPIO_DM_OD_DRIVESLOW,1,HSIOM_SEL_GPIO);
    }
    k_busy_wait(10);
    bool idle=wait_scl(); unsigned pulses=0;
    bool was_stuck=!Cy_GPIO_Read(GPIO_PRT8,1) || !idle;
    if (idle && was_stuck) {
        for (pulses=0;pulses<9;pulses++) {
            Cy_GPIO_Clr(GPIO_PRT8,0); k_busy_wait(10);
            Cy_GPIO_Set(GPIO_PRT8,0);
            if (!wait_scl()) { idle=false; break; }
            k_busy_wait(10);
        }
        if (idle) {
            /* STOP: SDA rises only after SCL has been released high. */
            Cy_GPIO_Clr(GPIO_PRT8,0);
            Cy_GPIO_Clr(GPIO_PRT8,1); k_busy_wait(10);
            Cy_GPIO_Set(GPIO_PRT8,0); idle=wait_scl(); k_busy_wait(10);
            Cy_GPIO_Set(GPIO_PRT8,1); k_busy_wait(10);
        }
    }
    bool clear=idle && Cy_GPIO_Read(GPIO_PRT8,1);
    for (int pin=0;pin<2;pin++)
        Cy_GPIO_Pin_FastInit(GPIO_PRT8,pin,mode[pin],out[pin],mux[pin]);
    printk("%s SENSOR I2C_BUS_CLEAR stuck=%u pulses=%u clear=%u\n",
           clear?"OK":"FAIL",was_stuck,pulses,clear);
    return clear?0:-EBUSY;
}
SYS_INIT(sensor_bus_clear, POST_KERNEL, 49);
