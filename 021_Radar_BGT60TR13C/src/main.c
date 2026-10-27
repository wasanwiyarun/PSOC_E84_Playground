#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <cy_sysclk.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/crc.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "radar.h"

static uint16_t capture[RADAR_SAMPLES*RADAR_MAX_FRAMES];
static int init_ret;
static const struct device *uart=DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
/* Match the ECO metadata to Chapter 017's secure-core clock configuration
 * before Zephyr initializes its peripheral clocks and UART. */
static int clock_metadata_init(void)
{ Cy_SysClk_EcoSetFrequency(17203200U); return 0; }
SYS_INIT(clock_metadata_init,PRE_KERNEL_1,0);

static void status(const char *token)
{
    uint32_t id=0; int ret=radar_id(&id);
    printk("%s RADAR INFO token=%s ret=%d chip_id=%06x spi_hz=%u samples_per_frame=%u max_frames=%u mode=idle\n",
           ret?"FAIL":"OK",token,ret,id,radar_spi_hz(),RADAR_SAMPLES,RADAR_MAX_FRAMES);
}
static void command(char *line)
{
    char *args[5],*save; unsigned argc=0;
    for(char *s=strtok_r(line," \t",&save);s;s=strtok_r(NULL," \t",&save)) {
        if(argc==ARRAY_SIZE(args)) { printk("FAIL RADAR COMMAND\n"); return; }
        args[argc++]=s;
    }
    if(argc<3 || strcmp(args[0],"radar")) { printk("FAIL RADAR COMMAND\n"); return; }
    const char *cmd=args[1],*token=args[argc-1];
    if(strlen(token)>32) { printk("FAIL RADAR TOKEN\n"); return; }
    if(argc==3 && (!strcmp(cmd,"info") || !strcmp(cmd,"id"))) { status(token); return; }
    if(argc==3 && !strcmp(cmd,"reset")) {
        init_ret=radar_init();
        printk("%s RADAR RESET token=%s ret=%d\n",init_ret?"FAIL":"OK",token,init_ret); return;
    }
    bool test=!strcmp(cmd,"selftest");
    if((test || !strcmp(cmd,"capture")) && argc==4) {
        char *end; unsigned long n=strtoul(args[2],&end,10);
        if(!*args[2] || *end || args[2][0]=='-' || !n || n>RADAR_MAX_FRAMES) {
            printk("FAIL RADAR ARGUMENT token=%s\n",token); return;
        }
        printk("OK RADAR START token=%s test=%u requested_frames=%lu\n",token,test,n);
        struct radar_result result;
        int ret=radar_capture(test,n,capture,&result);
        if(!ret) {
            for(unsigned f=0;f<result.frames;f++) {
                uint16_t lo=4095,hi=0; uint32_t sum=0;
                for(unsigned i=0;i<RADAR_SAMPLES;i++) {
                    uint16_t v=capture[f*RADAR_SAMPLES+i];
                    if(v<lo) lo=v;
                    if(v>hi) hi=v;
                    sum+=v;
                }
                printk("DATA RADAR token=%s frame=%u min=%u max=%u sum=%u raw=",token,f,lo,hi,sum);
                for(unsigned i=0;i<RADAR_SAMPLES;i++) printk("%03x",capture[f*RADAR_SAMPLES+i]);
                printk("\n");
            }
        }
        printk("%s RADAR DONE token=%s ret=%d test=%u frames=%u samples=%u elapsed_us=%u mismatches=%u irq_observed=%u fstat=%06x crc=%08x\n",
               ret?"FAIL":"OK",token,ret,test,result.frames,result.samples,result.elapsed_us,
               result.mismatches,result.irq_observed,result.fstat,
               crc32_ieee((const uint8_t *)capture,result.samples*sizeof(uint16_t)));
        return;
    }
    printk("FAIL RADAR COMMAND token=%s\n",token);
}
int main(void)
{
    printk("RADAR BOOT chapter=021\n");
    init_ret=radar_init();
    printk("%s RADAR INITIALIZATION_%s ret=%d chapter=021\n",
           init_ret?"FAIL":"OK",init_ret?"FAILED":"SUCCESS",init_ret);
    status("boot");
    char line[128]; unsigned used=0; bool overflow=false;
    for(;;) {
        unsigned char c;
        while(!uart_poll_in(uart,&c)) {
            if(c=='\r' || c=='\n') {
                if(overflow) printk("FAIL RADAR COMMAND_TOO_LONG\n");
                else if(used) { line[used]=0; command(line); }
                used=0; overflow=false;
            } else if(used<sizeof(line)-1) line[used++]=c;
            else overflow=true;
        }
        k_msleep(1);
    }
}
