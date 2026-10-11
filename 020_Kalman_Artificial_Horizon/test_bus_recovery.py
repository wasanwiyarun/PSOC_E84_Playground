#!/usr/bin/env python3
"""Compile the actual boot bus-clear routine against a bounded GPIO model."""
import pathlib
import subprocess
import tempfile

ROOT=pathlib.Path(__file__).resolve().parent
STUB=r'''
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#define CONFIG_I2C_INIT_PRIORITY 50
#define CONFIG_SENSOR_INIT_PRIORITY 90
#define BUILD_ASSERT(x) _Static_assert(x,#x)
#define SYS_INIT(fn,level,prio)
#define printk(...) printf(__VA_ARGS__)
#define CY_GPIO_DM_OD_DRIVESLOW 12
#define HSIOM_SEL_GPIO 0
typedef int en_hsiom_sel_t;
typedef struct { int unused; } GPIO_PRT_Type;
static GPIO_PRT_Type port;
#define GPIO_PRT8 (&port)
static unsigned modes[2], muxes[2], outs[2];
static unsigned pulses, release_after, wait_us;
static bool scl_stuck;
static unsigned Cy_GPIO_GetHSIOM(GPIO_PRT_Type *p,unsigned n) {(void)p;return muxes[n];}
static unsigned Cy_GPIO_GetDrivemode(GPIO_PRT_Type *p,unsigned n) {(void)p;return modes[n];}
static unsigned Cy_GPIO_ReadOut(GPIO_PRT_Type *p,unsigned n) {(void)p;return outs[n];}
static void Cy_GPIO_Pin_FastInit(GPIO_PRT_Type *p,unsigned n,unsigned m,unsigned o,unsigned h)
{(void)p;modes[n]=m;outs[n]=o;muxes[n]=h;}
static unsigned Cy_GPIO_Read(GPIO_PRT_Type *p,unsigned n)
{(void)p;return outs[n] && (n ? pulses>=release_after : !scl_stuck);}
static void Cy_GPIO_Clr(GPIO_PRT_Type *p,unsigned n) {(void)p;outs[n]=0;}
static void Cy_GPIO_Set(GPIO_PRT_Type *p,unsigned n)
{
    (void)p;assert(modes[n]==CY_GPIO_DM_OD_DRIVESLOW);
    if(n==0 && !outs[0] && outs[1]) pulses++;
    outs[n]=1;
}
static void k_busy_wait(unsigned us) {wait_us+=us;assert(wait_us<20000);}
'''
HARNESS=r'''
#include "stub.h"
#include "sensor_bus_recovery.c"
static void test(unsigned release,bool clock_stuck,int expected,unsigned clocks)
{
    pulses=wait_us=0; release_after=release; scl_stuck=clock_stuck;
    for(int i=0;i<2;i++){modes[i]=0;muxes[i]=27;outs[i]=1;}
    assert(sensor_bus_clear()==expected);
    assert(pulses==clocks);
    for(int i=0;i<2;i++) assert(modes[i]==0 && muxes[i]==27 && outs[i]==1);
}
int main(void)
{
    test(0,false,0,0);
    test(9,false,0,9);
    test(99,false,-EBUSY,9);
    test(0,true,-EBUSY,0);
    puts("PASS bus clear: idle untouched, 9-clock recovery, stuck SDA/SCL bounded, pin state restored");
}
'''

with tempfile.TemporaryDirectory(prefix="e84-bus-clear-") as tmp:
    p=pathlib.Path(tmp); (p/"zephyr").mkdir()
    (p/"stub.h").write_text(STUB)
    for f in ("cy_pdl.h","zephyr/init.h","zephyr/kernel.h"):
        (p/f).write_text('#include "stub.h"\n')
    (p/"test.c").write_text(HARNESS)
    subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror",f"-I{p}",
                    f"-I{ROOT/'src'}",str(p/"test.c"),"-o",str(p/"test")],check=True)
    subprocess.run([str(p/"test")],check=True)
