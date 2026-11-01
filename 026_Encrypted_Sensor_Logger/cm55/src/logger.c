/* UI, acquisition and storage are separate threads. Only the storage worker
 * owns LittleFS and cryptographic state. No plaintext ever goes to flash. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <lvgl.h>
#include <lvgl_zephyr.h>
#include "store.h"
#include "ft5406_touch.h"
#include "waveshare_4p3_display.h"

static const struct device *const display=DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const imu=DEVICE_DT_GET(DT_ALIAS(accel0));
static const struct device *const baro=DEVICE_DT_GET(DT_ALIAS(pressure_sensor));
static K_MUTEX_DEFINE(lock);
/* Flash/secure-service calls can stall XIP interrupt service. Do not start
 * them in the middle of a sensor transaction on SCB0. */
static K_MUTEX_DEFINE(acquisition_gate);
K_MSGQ_DEFINE(samples,SAMPLE_BYTES,128,4);
K_MSGQ_DEFINE(commands,96,4,4);
static K_THREAD_STACK_DEFINE(sensor_stack,3072);
static K_THREAD_STACK_DEFINE(storage_stack,8192);
static struct k_thread sensor_thread,storage_thread;
struct status {
    struct store_status disk;
    uint64_t time_ms;
    int32_t values[8];
    uint32_t acquired,errors,dropped,ui_frames;
    bool valid,recording,display_ok,touch_ok;
    int error;
};
static struct status state;
static lv_obj_t *mode_label,*count_label,*acc_label,*gyro_label,*env_label,*space_label,*note_label;
static uint8_t batch[SAMPLE_BYTES*BATCH_SAMPLES];
static unsigned buffered;

static int clock_metadata(void) { Cy_SysClk_EcoSetFrequency(17203200); return 0; }
SYS_INIT(clock_metadata,PRE_KERNEL_1,0);
static struct status snapshot(void)
{ k_mutex_lock(&lock,K_FOREVER); struct status v=state; k_mutex_unlock(&lock); return v; }
static void disk_publish(int error)
{
    struct store_status disk; store_snapshot(&disk);
    k_mutex_lock(&lock,K_FOREVER); state.disk=disk;
    if(error) state.error=error;
    k_mutex_unlock(&lock);
}
static void recording_set(bool on)
{ k_mutex_lock(&lock,K_FOREVER); state.recording=on; k_mutex_unlock(&lock); }
static void fail_recording(int error)
{
    recording_set(false); store_abort(); k_msgq_purge(&samples); buffered=0;
    disk_publish(error); printk("EVENT LOGGER STOPPED_ERROR rc=%d\n",error);
}
static void acquire(void *a,void *b,void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    int64_t next=k_uptime_get();
    for(;;) {
        struct sensor_value av[3],gv[3],temp,press;
        k_mutex_lock(&acquisition_gate,K_FOREVER);
        int ret=device_is_ready(imu)&&device_is_ready(baro)?0:-ENODEV;
        if(!ret) ret=sensor_sample_fetch(imu);
        if(ret) printk("EVENT SENSOR_FAILED stage=imu rc=%d\n",ret);
        if(!ret) ret=sensor_channel_get(imu,SENSOR_CHAN_ACCEL_XYZ,av);
        if(!ret) ret=sensor_channel_get(imu,SENSOR_CHAN_GYRO_XYZ,gv);
        if(!ret) {
            ret=sensor_sample_fetch(baro);
            if(ret) printk("EVENT SENSOR_FAILED stage=baro rc=%d\n",ret);
        }
        if(!ret) ret=sensor_channel_get(baro,SENSOR_CHAN_AMBIENT_TEMP,&temp);
        if(!ret) ret=sensor_channel_get(baro,SENSOR_CHAN_PRESS,&press);
        k_mutex_unlock(&acquisition_gate);
        uint8_t packed[SAMPLE_BYTES]; uint64_t now=k_uptime_get(); int32_t v[8];
        if(!ret) {
            for(unsigned i=0;i<3;i++) {
                v[i]=sensor_value_to_micro(&av[i])/1000; /* milli m/s^2 */
                v[3+i]=sensor_value_to_micro(&gv[i])/1000; /* milli rad/s */
            }
            v[6]=sensor_value_to_micro(&temp)/1000; /* milli Celsius */
            v[7]=sensor_value_to_micro(&press)/1000; /* kPa -> Pa */
            sys_put_le64(now,packed);
            for(unsigned i=0;i<8;i++) sys_put_le32((uint32_t)v[i],packed+8+4*i);
        }
        k_mutex_lock(&lock,K_FOREVER);
        state.valid=!ret;
        if(ret) { state.errors++; if(state.recording) state.error=ret; }
        else {
            state.time_ms=now; state.acquired++; memcpy(state.values,v,sizeof(v));
            if(state.recording && k_msgq_put(&samples,packed,K_NO_WAIT)) {
                state.dropped++; state.error=-ENOBUFS;
            }
        }
        k_mutex_unlock(&lock);
        next+=100;
        int64_t delay=next-k_uptime_get();
        if(delay<0) { next=k_uptime_get(); delay=0; }
        k_msleep(delay);
    }
}
static int drain(bool flush)
{
    while(!k_msgq_get(&samples,batch+buffered*SAMPLE_BYTES,K_NO_WAIT)) {
        if(++buffered==BATCH_SAMPLES) {
            int ret=store_append(batch,buffered); if(ret) return ret;
            buffered=0; disk_publish(0);
        }
    }
    if(flush && buffered) {
        int ret=store_append(batch,buffered); if(ret) return ret;
        buffered=0; disk_publish(0);
    }
    return 0;
}
static void status_print(void)
{
    disk_publish(0); struct status v=snapshot();
    printk("OK STATUS mounted=%u key=%u recording=%u samples=%u batches=%u bytes=%u free=%u nonce=%llu name=%s acquired=%u errors=%u dropped=%u sensor=%u display=%u touch=%u renders=%u rc=%d\n",
        v.disk.mounted,v.disk.key_ready,v.recording,v.disk.samples,v.disk.batches,
        v.disk.bytes,v.disk.free_bytes,(unsigned long long)v.disk.nonce,
        v.disk.name[0]?v.disk.name:"-",v.acquired,v.errors,v.dropped,v.valid,
        v.display_ok,v.touch_ok,v.ui_frames,v.error);
}
static int parse_index(const char *text,unsigned *value)
{
    if(!text || !*text) return -EINVAL;
    for(const char *p=text;*p;p++) if(*p<'0'||*p>'9') return -EINVAL;
    char *end; unsigned long n=strtoul(text,&end,10);
    if(*end || n>100000) return -EINVAL;
    *value=n; return 0;
}
static void command(char *line)
{
    char *save=NULL,*args[6]; unsigned argc=0;
    for(char *p=strtok_r(line," ",&save);p;p=strtok_r(NULL," ",&save)) {
        if(argc==ARRAY_SIZE(args)) { printk("ERR COMMAND rc=%d\n",-EINVAL); return; }
        args[argc++]=p;
    }
    if(argc<2 || strcmp(args[0],"log")) { printk("ERR COMMAND rc=%d\n",-EINVAL); return; }
    const char *cmd=args[1]; int ret=-EINVAL; unsigned index=0; bool handled=false;
    struct status v=snapshot();
    if(!strcmp(cmd,"status") && argc==2) { status_print(); return; }
    if(!strcmp(cmd,"init-blank") && argc==2) {
        ret=store_init_blank();
        if(!ret) { k_mutex_lock(&lock,K_FOREVER); state.error=0; k_mutex_unlock(&lock); }
    }
    else if(!strcmp(cmd,"start") && argc==3) {
        char name[25]; const char *selected=args[2];
        if(!strcmp(selected,"auto")) {
            snprintk(name,sizeof(name),"r%016llx",(unsigned long long)(v.disk.nonce+1)); selected=name;
        }
        ret=v.recording?-EBUSY:!v.valid?-ENODEV:store_start(selected);
        if(!ret) {
            k_msgq_purge(&samples); buffered=0;
            k_mutex_lock(&lock,K_FOREVER); state.error=0; state.recording=true; k_mutex_unlock(&lock);
        }
    } else if(!strcmp(cmd,"stop") && argc==2) {
        ret=v.recording?0:-EALREADY;
        if(!ret) { recording_set(false); ret=drain(true); if(!ret) ret=store_stop(); }
        if(ret && ret!=-EALREADY) fail_recording(ret);
    } else if(!strcmp(cmd,"list") && argc==2) { ret=store_list(); handled=true; }
    else if((!strcmp(cmd,"read")||!strcmp(cmd,"verify")) && argc==3) {
        ret=store_read(args[2],!strcmp(cmd,"read")); handled=true;
    } else if(!strcmp(cmd,"delete") && argc==3) ret=store_delete(args[2]);
    else if(!strcmp(cmd,"raw") && argc==4 && !parse_index(args[3],&index)) {
        ret=store_raw(args[2],index); handled=true;
    } else if(!strcmp(cmd,"tamper") && argc==5 && !parse_index(args[3],&index))
        ret=store_tamper(args[2],index,args[4]);
    else if(!strcmp(cmd,"kat") && argc==2) ret=store_kat();
    else if(!strcmp(cmd,"reboot") && argc==2) {
        printk("OK REBOOT unsynced_data_may_be_lost=1\n"); k_msleep(50); sys_reboot(SYS_REBOOT_COLD); return;
    }
    disk_publish(0);
    if(ret) printk("ERR COMMAND cmd=%s rc=%d\n",cmd,ret);
    else if(!handled) printk("OK COMMAND cmd=%s\n",cmd);
}
static void storage(void *a,void *b,void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    k_mutex_lock(&acquisition_gate,K_FOREVER);
    int ret=store_init(); disk_publish(ret);
    k_mutex_unlock(&acquisition_gate);
    printk("READY LOGGER version=1 storage_rc=%d\n",ret);
    char line[96];
    for(;;) {
        k_mutex_lock(&acquisition_gate,K_FOREVER);
        if(!k_msgq_get(&commands,line,K_NO_WAIT)) command(line);
        struct status v=snapshot();
        if(v.recording) {
            if(v.error) fail_recording(v.error);
            else { ret=drain(false); if(ret) fail_recording(ret); }
        }
        k_mutex_unlock(&acquisition_gate);
        k_msleep(5);
    }
}
static void submit(const char *text)
{
    char line[96]={0}; strncpy(line,text,sizeof(line)-1);
    if(k_msgq_put(&commands,line,K_NO_WAIT)) printk("ERR QUEUE rc=%d\n",-EBUSY);
}
static void button_click(lv_event_t *event)
{ submit(lv_event_get_user_data(event)); }
static lv_obj_t *label(lv_obj_t *parent,int x,int y,int width,const char *text,uint32_t color)
{
    lv_obj_t *o=lv_label_create(parent); lv_obj_set_pos(o,x,y); lv_obj_set_width(o,width);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_font(o,&lv_font_montserrat_20,0); lv_label_set_text(o,text); return o;
}
static lv_obj_t *card(int x,int y,int width,int height,const char *title)
{
    lv_obj_t *o=lv_obj_create(lv_screen_active()); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,width,height);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x13283b),0);
    lv_obj_set_style_border_color(o,lv_color_hex(0x25445e),0);
    lv_obj_set_style_pad_all(o,12,0); lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    label(o,0,0,width-28,title,0x64cce3); return o;
}
static void button(int x,int width,const char *text,const char *cmd,uint32_t color)
{
    lv_obj_t *o=lv_button_create(lv_screen_active()); lv_obj_set_pos(o,x,414); lv_obj_set_size(o,width,48);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
    lv_obj_t *t=lv_label_create(o); lv_label_set_text(t,text); lv_obj_center(t);
    lv_obj_set_style_text_font(t,&lv_font_montserrat_20,0);
    lv_obj_add_event_cb(o,button_click,LV_EVENT_CLICKED,(void *)cmd);
}
static void create_ui(void)
{
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(0x081420),0);
    label(lv_screen_active(),24,18,530,"SECURE SENSOR LOGGER / 026",0xffffff);
    label(lv_screen_active(),570,18,220,"AES-256-GCM",0x65efc0);
    lv_obj_t *top=card(24,60,752,105,"LIVE / 10 Hz / BMI270 + DPS368");
    mode_label=label(top,0,34,230,"INITIALIZING",0x65efc0);
    lv_obj_set_style_text_font(mode_label,&lv_font_montserrat_28,0);
    count_label=label(top,250,34,470,"Waiting for storage",0xffffff);
    lv_obj_t *left=card(24,180,365,93,"ACCELEROMETER / milli m/s2");
    acc_label=label(left,0,32,335,"--",0xffffff);
    lv_obj_t *right=card(405,180,371,93,"GYROSCOPE / milli rad/s");
    gyro_label=label(right,0,32,341,"--",0xffffff);
    left=card(24,285,365,90,"ENVIRONMENT"); env_label=label(left,0,32,335,"--",0xffffff);
    right=card(405,285,371,90,"ENCRYPTED STORAGE"); space_label=label(right,0,32,341,"--",0xffffff);
    note_label=label(lv_screen_active(),24,381,750,"Persistent TF-M key / authenticated record chain",0x83a4ba);
    button(24,237,"RECORD","log start auto",0x087b64);
    button(281,237,"STOP + SAVE","log stop",0x256fa2);
    button(538,238,"STATUS","log status",0x334a61);
}
static void update_ui(void)
{
    struct status v=snapshot(); char text[100];
    lv_label_set_text(mode_label,v.recording?"RECORDING":v.error?"CHECK STATUS":"READY");
    snprintk(text,sizeof(text),"%u samples / %u batches\n%s",v.disk.samples,v.disk.batches,
             v.disk.name[0]?v.disk.name:"No active recording"); lv_label_set_text(count_label,text);
    snprintk(text,sizeof(text),"X %d   Y %d   Z %d",v.values[0],v.values[1],v.values[2]); lv_label_set_text(acc_label,text);
    snprintk(text,sizeof(text),"X %d   Y %d   Z %d",v.values[3],v.values[4],v.values[5]); lv_label_set_text(gyro_label,text);
    snprintk(text,sizeof(text),"%d.%02d C    %d Pa",v.values[6]/1000,abs(v.values[6]%1000)/10,v.values[7]); lv_label_set_text(env_label,text);
    snprintk(text,sizeof(text),"%u KiB free / %u B saved",v.disk.free_bytes/1024,v.disk.bytes); lv_label_set_text(space_label,text);
    if(!v.disk.mounted) lv_label_set_text(note_label,"Storage unavailable: UART log init-blank only on erased partition");
    else if(v.error) { snprintk(text,sizeof(text),"Error %d / drops %u / sensor errors %u",v.error,v.dropped,v.errors); lv_label_set_text(note_label,text); }
    else lv_label_set_text(note_label,"TF-M key protected / STOP seals recording / UART exports verified data");
    k_mutex_lock(&lock,K_FOREVER); state.ui_frames++; k_mutex_unlock(&lock);
}
int main(void)
{
    const struct device *uart=DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    if(!device_is_ready(display)) { printk("FAIL LOGGER DISPLAY_DEVICE\n"); return 1; }
    int ret=lvgl_init(); if(ret) { printk("FAIL LOGGER LVGL rc=%d\n",ret); return 1; }
    ret=ft5406_init(); state.touch_ok=!ret;
    if(!ret) ft5406_lvgl_register();
    create_ui(); lv_refr_now(NULL); display_blanking_off(display);
    const struct sensor_value hz={.val1=100};
    if(!device_is_ready(imu) || !device_is_ready(baro)) {
        printk("FAIL LOGGER SENSOR_DEVICE\n"); return 1;
    }
    ret=sensor_attr_set(imu,SENSOR_CHAN_ACCEL_XYZ,SENSOR_ATTR_SAMPLING_FREQUENCY,&hz);
    if(!ret) ret=sensor_attr_set(imu,SENSOR_CHAN_GYRO_XYZ,SENSOR_ATTR_SAMPLING_FREQUENCY,&hz);
    if(ret) { printk("FAIL LOGGER SENSOR_ODR rc=%d\n",ret); return 1; }
    /* Report startup before the storage worker can print concurrently. */
    struct waveshare_status initial;
    k_msleep(100);
    ret=waveshare_get_status(display,&initial);
    if(ret) { printk("FAIL LOGGER DISPLAY_HEALTH rc=%d\n",ret); return 1; }
    state.display_ok=true;
    printk("OK DISPLAY INITIALIZATION_SUCCESS frames=%u clock=%u dsi0=%x dsi1=%x dc=%x touch=%u\n",
        initial.frames,initial.clock_hz,initial.dsi_errors0,initial.dsi_errors1,initial.dc_errors,state.touch_ok);
    k_thread_priority_set(k_current_get(),5);
    k_thread_create(&sensor_thread,sensor_stack,K_THREAD_STACK_SIZEOF(sensor_stack),acquire,NULL,NULL,NULL,3,0,K_NO_WAIT);
    k_thread_create(&storage_thread,storage_stack,K_THREAD_STACK_SIZEOF(storage_stack),storage,NULL,NULL,NULL,6,0,K_NO_WAIT);
    char line[96]; size_t used=0; bool overflow=false; int64_t next=0;
    for(;;) {
        k_mutex_lock(&acquisition_gate,K_FOREVER);
        if(state.touch_ok) ft5406_poll();
        k_mutex_unlock(&acquisition_gate);
        if(k_uptime_get()>=next) {
            update_ui(); next=k_uptime_get()+200;
            struct waveshare_status d;
            k_mutex_lock(&acquisition_gate,K_FOREVER);
            ret=waveshare_get_status(display,&d);
            k_mutex_unlock(&acquisition_gate);
            k_mutex_lock(&lock,K_FOREVER); state.display_ok=!ret; k_mutex_unlock(&lock);
        }
        unsigned char ch;
        while(!uart_poll_in(uart,&ch)) {
            if(ch=='\n'||ch=='\r') {
                if(overflow) printk("ERR COMMAND_TOO_LONG\n");
                else if(used) { line[used]=0; submit(line); }
                used=0; overflow=false;
            } else if(used<sizeof(line)-1) line[used++]=ch;
            else overflow=true;
        }
        lv_timer_handler(); k_msleep(3);
    }
}
