#include <zephyr/drivers/display.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <cy_sysclk.h>
#include <lvgl.h>
#include <lvgl_zephyr.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "attitude.h"
#include "flight_math.h"
#include "magnetometer.h"
#include "horizon_draw.h"
#include "ft5406_touch.h"
#include "waveshare_4p3_display.h"

static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *imu = DEVICE_DT_GET(DT_ALIAS(accel0));
static const struct device *pressure = DEVICE_DT_GET(DT_ALIAS(pressure_sensor));
static K_MUTEX_DEFINE(state_lock);
static K_THREAD_STACK_DEFINE(sensor_stack, 4096);
static K_THREAD_STACK_DEFINE(mag_stack, 4096);
static K_THREAD_STACK_DEFINE(baro_stack, 2048);
static struct k_thread sensor_thread;
static struct k_thread mag_thread;
static struct k_thread baro_thread;
static struct attitude estimator;
static struct cockpit_frame frame;
static struct baro_filter baro;
struct telemetry {
    struct magnetometer_status mag;
    float roll, pitch, yaw, g, gyro[3];
    float pressure_kpa, temperature, altitude_m, climb_mps;
    float reference_kpa;
    uint32_t baro_samples, baro_errors, zeros;
    int64_t baro_ms;
    int baro_ret;
    uint32_t samples, corrections, rejected, calibration_count, calibrations;
    int64_t sample_ms;
    int ret;
    bool calibrated, calibrating, ground_frame;
};
static struct telemetry state;
static bool request_calibration = true;
static bool held;
static int64_t zero_message_until;
static uint32_t renders, raster_crc, render_us;
static uint16_t horizon_pixels[HORIZON_SIZE * HORIZON_SIZE]
    __attribute__((section("GFX_MEM"), aligned(128)));
static const lv_image_dsc_t horizon_bitmap = {
    .header = {.magic=LV_IMAGE_HEADER_MAGIC, .cf=LV_COLOR_FORMAT_RGB565,
               .w=HORIZON_SIZE, .h=HORIZON_SIZE, .stride=HORIZON_SIZE*2},
    .data_size=sizeof(horizon_pixels), .data=(const uint8_t *)horizon_pixels,
};
static lv_obj_t *horizon, *roll_label, *pitch_label, *yaw_label, *g_label;
static lv_obj_t *gyro_label, *status_label, *hold_label, *rate_label;
static lv_obj_t *mag_label, *mag_note;
static lv_obj_t *alt_label, *climb_label, *pressure_label, *baro_note;
static lv_obj_t *heading_lines[24], *heading_labels[4];
static lv_point_precise_t heading_ticks[24][2];
/* Fixed lubber marker and aircraft symbol: these are never rotated. */
static const lv_point_precise_t heading_marker[]={{70,3},{76,11},{82,3}};
static const lv_point_precise_t heading_body[]={{76,60},{76,96}};
static const lv_point_precise_t heading_wings[]={{53,78},{99,78}};
static const lv_point_precise_t heading_tail[]={{67,92},{85,92}};
static float displayed_heading;

static int display_clock_metadata_init(void)
{ Cy_SysClk_EcoSetFrequency(17203200U); return 0; }
SYS_INIT(display_clock_metadata_init, PRE_KERNEL_1, 0);

static float value(const struct sensor_value *v)
{ return (float)v->val1 + (float)v->val2 * 0.000001f; }

static struct telemetry snapshot(void)
{
    k_mutex_lock(&state_lock,K_FOREVER);
    struct telemetry copy=state;
    k_mutex_unlock(&state_lock);
    return copy;
}

static void baro_worker(void *a0, void *a1, void *a2)
{
    ARG_UNUSED(a0); ARG_UNUSED(a1); ARG_UNUSED(a2);
    int64_t previous=0,next=k_uptime_get();
    while(true) {
        struct sensor_value pv,tv;
        /* Driver conversion sleeps here, never inside the 100 Hz IMU task.
         * SCB0 driver serializes individual transfers shared with BMI270. */
        int ret=device_is_ready(pressure)?sensor_sample_fetch(pressure):-ENODEV;
        if(!ret) ret=sensor_channel_get(pressure,SENSOR_CHAN_PRESS,&pv);
        if(!ret) ret=sensor_channel_get(pressure,SENSOR_CHAN_AMBIENT_TEMP,&tv);
        int64_t now=k_uptime_get();
        k_mutex_lock(&state_lock,K_FOREVER);
        if(!ret) {
            float dt=previous?(now-previous)*0.001f:0.1f;
            if(dt>60) dt=60;
            if(baro_update(&baro,value(&pv),dt)) {
                state.pressure_kpa=value(&pv); state.temperature=value(&tv);
                state.altitude_m=baro.altitude_m; state.climb_mps=baro.climb_mps;
                state.reference_kpa=baro.reference_kpa;
                state.baro_ms=now; state.baro_samples++; previous=now;
            } else ret=-ERANGE;
        }
        state.baro_ret=ret;
        if(ret) state.baro_errors++;
        k_mutex_unlock(&state_lock);
        next+=100;
        int64_t wait=next-k_uptime_get();
        if(wait>0) k_msleep((int32_t)wait);
        else { next=k_uptime_get(); k_msleep(10); }
    }
}

static void mag_worker(void *a0, void *a1, void *a2)
{
    ARG_UNUSED(a0); ARG_UNUSED(a1); ARG_UNUSED(a2);
    /* The PDL's blocking CCC/FIFO-reset paths have their own timeout.
     * Keep them below both the IMU and UI priorities, on a dedicated bus. */
    magnetometer_init();
    while (true) {
        magnetometer_read();
        struct magnetometer_status s;
        magnetometer_status(&s);
        k_mutex_lock(&state_lock,K_FOREVER);
        state.mag=s;
        k_mutex_unlock(&state_lock);
        k_msleep(5);
    }
}

static void sample_worker(void *a0, void *a1, void *a2)
{
    ARG_UNUSED(a0); ARG_UNUSED(a1); ARG_UNUSED(a2);
    float bias[3]={0}, sums[3]={0};
    int64_t last_us=0, next_ms=k_uptime_get();
    while (true) {
        struct sensor_value av[3], wv[3];
        int ret=sensor_sample_fetch(imu);
        if (!ret) ret=sensor_channel_get(imu,SENSOR_CHAN_ACCEL_XYZ,av);
        if (!ret) ret=sensor_channel_get(imu,SENSOR_CHAN_GYRO_XYZ,wv);
        int64_t now_us=k_ticks_to_us_floor64(k_uptime_ticks());
        k_mutex_lock(&state_lock,K_FOREVER);
        state.ret=ret;
        if (!ret) {
            float acc[3], raw[3], gyro[3];
            for(int i=0;i<3;i++) { acc[i]=value(&av[i])/9.80665f; raw[i]=value(&wv[i]); }
            if (request_calibration) {
                request_calibration=false; state.calibrating=true;
                state.calibration_count=0; memset(sums,0,sizeof(sums));
            }
            float norm=sqrtf(acc[0]*acc[0]+acc[1]*acc[1]+acc[2]*acc[2]);
            float rate=sqrtf(raw[0]*raw[0]+raw[1]*raw[1]+raw[2]*raw[2]);
            if (state.calibrating) {
                if (norm>0.97f && norm<1.03f && rate<0.0873f) {
                    for(int i=0;i<3;i++) sums[i]+=raw[i];
                    if (++state.calibration_count>=100) {
                        for(int i=0;i<3;i++) bias[i]=sums[i]/100.0f;
                        state.calibrating=false; state.calibrated=true;
                        state.calibrations++;
                        attitude_reset(&estimator);
                    }
                } else { state.calibration_count=0; memset(sums,0,sizeof(sums)); }
            }
            for(int i=0;i<3;i++) gyro[i]=raw[i]-bias[i];
            cockpit_apply(&frame,acc,acc);
            cockpit_apply(&frame,gyro,gyro);
            cockpit_to_pilot(gyro,state.gyro);
            for(int i=0;i<3;i++) state.gyro[i]*=57.2957795f;
            float dt=last_us?(now_us-last_us)*0.000001f:0.01f;
            if (attitude_update(&estimator,acc,gyro,dt)) {
                float filtered[3]={estimator.roll.angle,estimator.pitch.angle,estimator.yaw};
                float pilot[3]; cockpit_to_pilot(filtered,pilot);
                state.roll=pilot[0]; state.pitch=pilot[1]; state.yaw=pilot[2];
                state.g=estimator.g;
                state.corrections+=estimator.accel_used;
                state.samples++; state.sample_ms=k_uptime_get();
            } else { state.rejected++; state.ret=-ERANGE; }
        }
        last_us=now_us;
        k_mutex_unlock(&state_lock);
        next_ms+=10;
        int64_t delay=next_ms-k_uptime_get();
        if(delay>0) k_msleep((int32_t)delay);
        else { next_ms=k_uptime_get(); k_msleep(1); }
    }
}

static bool zero_view(void)
{
    k_mutex_lock(&state_lock,K_FOREVER);
    bool valid=estimator.initialized && state.samples && !state.ret && !state.calibrating &&
               k_uptime_get()-state.sample_ms<150;
    if(valid) {
        /* Reference rotation operates in filter coordinates, not pilot signs. */
        cockpit_zero(&frame,estimator.roll.angle,estimator.pitch.angle);
        attitude_reset(&estimator);
        state.roll=state.pitch=state.yaw=0;
        state.zeros++;
        state.ground_frame=true;
        if(state.baro_samples && !state.baro_ret && k_uptime_get()-state.baro_ms<500) {
            baro_zero(&baro,state.pressure_kpa);
            state.altitude_m=state.climb_mps=0;
            state.reference_kpa=baro.reference_kpa;
        }
    }
    k_mutex_unlock(&state_lock);
    if(valid) { held=false; zero_message_until=k_uptime_get()+3000; }
    return valid;
}

static void upright_view(void)
{
    k_mutex_lock(&state_lock,K_FOREVER);
    cockpit_upright(&frame); attitude_reset(&estimator);
    state.ground_frame=false;
    k_mutex_unlock(&state_lock);
    held=false;
}
static void calibrate(void)
{
    k_mutex_lock(&state_lock,K_FOREVER);
    request_calibration=true; state.calibrating=true;
    k_mutex_unlock(&state_lock);
}
static void button_event(lv_event_t *event)
{
    intptr_t action=(intptr_t)lv_event_get_user_data(event);
    if(action==0 && !zero_view()) { printk("FAIL HORIZON ZERO_NOT_READY\n"); return; }
    if(action==1) calibrate();
    if(action==2) { held=!held; lv_label_set_text(hold_label,held?"RESUME":"HOLD VIEW"); }
    if(action==3) upright_view();
    lv_label_set_text(hold_label,held?"RESUME":"HOLD VIEW");
    printk("OK HORIZON TOUCH action=%d\n",(int)action);
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj=lv_label_create(parent);
    lv_label_set_text(obj,text); lv_obj_set_pos(obj,x,y);
    lv_obj_set_style_text_font(obj,font,0);
    lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    return obj;
}
static lv_obj_t *card(lv_obj_t *screen,int x,int y,int w,int h,const char *title)
{
    lv_obj_t *obj=lv_obj_create(screen);
    lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,w,h);
    lv_obj_set_style_pad_all(obj,12,0);
    lv_obj_set_style_radius(obj,12,0);
    lv_obj_set_style_border_width(obj,1,0);
    lv_obj_set_style_border_color(obj,lv_color_hex(0x23384D),0);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0x0C141F),0);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    label(obj,title,0,0,LV_FONT_DEFAULT,0x7F9AB4);
    return obj;
}

static void update_heading(float heading)
{
    for(int i=0;i<24;i++) {
        float p[2];
        cockpit_card_point(heading,i*15.0f,i%6?64:59,p);
        heading_ticks[i][0]=(lv_point_precise_t){76+p[0],76+p[1]};
        cockpit_card_point(heading,i*15.0f,70,p);
        heading_ticks[i][1]=(lv_point_precise_t){76+p[0],76+p[1]};
        lv_line_set_points(heading_lines[i],heading_ticks[i],2);
        lv_obj_invalidate(heading_lines[i]);
    }
    for(int i=0;i<4;i++) {
        float p[2]; cockpit_card_point(heading,i*90.0f,47,p);
        /* Move label centres with the scale; keep text upright for readability. */
        lv_obj_set_pos(heading_labels[i],(int32_t)lroundf(76+p[0])-18,
                       (int32_t)lroundf(76+p[1])-8);
    }
    displayed_heading=heading;
}

static void fixed_heading_line(lv_obj_t *dial,const lv_point_precise_t *points,int count)
{
    lv_obj_t *line=lv_line_create(dial);
    lv_line_set_points(line,points,count);
    lv_obj_set_style_line_width(line,3,0);
    lv_obj_set_style_line_color(line,lv_color_hex(0xFFD14F),0);
}

static void create_heading(lv_obj_t *parent)
{
    lv_obj_t *dial=lv_obj_create(parent);
    lv_obj_set_pos(dial,10,23); lv_obj_set_size(dial,156,156);
    lv_obj_set_style_pad_all(dial,0,0);
    lv_obj_set_style_radius(dial,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_border_color(dial,lv_color_hex(0x34536C),0);
    lv_obj_set_style_bg_color(dial,lv_color_hex(0x08121E),0);
    lv_obj_remove_flag(dial,LV_OBJ_FLAG_SCROLLABLE);
    for(int i=0;i<24;i++) {
        lv_obj_t *line=lv_line_create(dial);
        heading_lines[i]=line;
        lv_obj_set_style_line_width(line,i%6?1:2,0);
        lv_obj_set_style_line_color(line,lv_color_hex(0x7F9AB4),0);
    }
    /* Numbers deliberately replace N/E/S/W: this is not calibrated north. */
    const char *marks[]={"000","090","180","270"};
    for(int i=0;i<4;i++) {
        heading_labels[i]=label(dial,marks[i],0,0,LV_FONT_DEFAULT,0xECF5FF);
        lv_obj_set_width(heading_labels[i],36);
        lv_obj_set_style_text_align(heading_labels[i],LV_TEXT_ALIGN_CENTER,0);
    }
    update_heading(0);
    fixed_heading_line(dial,heading_marker,3);
    fixed_heading_line(dial,heading_body,2);
    fixed_heading_line(dial,heading_wings,2);
    fixed_heading_line(dial,heading_tail,2);
}

static void create_ui(void)
{
    lv_obj_t *screen=lv_screen_active();
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x060C14),0);
    lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    label(screen,"FLIGHT DECK / 020",16,12,&lv_font_montserrat_28,0xECF5FF);
    status_label=label(screen,"CALIBRATING - KEEP STILL",406,19,LV_FONT_DEFAULT,0x65E3D2);
    rate_label=label(screen,"EDUCATIONAL DEMO / NOT FOR FLIGHT",406,39,LV_FONT_DEFAULT,0x7F9AB4);
    lv_obj_t *c=card(screen,12,64,200,248,"DIRECTION / GYRO");
    create_heading(c);
    yaw_label=label(c,"000.0 deg",32,180,&lv_font_montserrat_20,0xFFD14F);
    label(c,"Relative only - drifts",0,207,LV_FONT_DEFAULT,0x7F9AB4);
    c=card(screen,12,320,200,92,"TURN RATE / G-LOAD");
    gyro_label=label(c,"Z +0.0 deg/s",0,24,&lv_font_montserrat_20,0x9DE9E0);
    g_label=label(c,"1.00 g",0,50,&lv_font_montserrat_20,0xECF5FF);
    horizon=lv_image_create(screen); lv_image_set_src(horizon,&horizon_bitmap);
    lv_obj_set_pos(horizon,224,56);
    roll_label=label(screen,"BANK +0.0",228,400,LV_FONT_DEFAULT,0x5EDBEC);
    pitch_label=label(screen,"PITCH +0.0",422,400,LV_FONT_DEFAULT,0xFFD14F);
    c=card(screen,588,64,200,128,"REL ALT / BARO EST.");
    alt_label=label(c,"-- m",0,25,&lv_font_montserrat_28,0xECF5FF);
    climb_label=label(c,"-- m/s",0,61,&lv_font_montserrat_20,0x5EDBEC);
    baro_note=label(c,"Waiting for DPS368",0,92,LV_FONT_DEFAULT,0x7F9AB4);
    c=card(screen,588,200,200,84,"PRESSURE / TEMP");
    pressure_label=label(c,"-- hPa\n-- C",0,24,LV_FONT_DEFAULT,0x9DE9E0);
    c=card(screen,588,292,200,120,"MAGNETIC NORTH");
    mag_label=label(c,"I3C STARTING",0,21,LV_FONT_DEFAULT,0x65E3D2);
    mag_note=label(c,"Not calibrated\nHeading unavailable",0,43,LV_FONT_DEFAULT,0x7F9AB4);
    label(c,"AIRSPEED: NO SENSOR",0,80,LV_FONT_DEFAULT,0x7F9AB4);
    const char *names[]={"SET GROUND ZERO","CAL GYRO","HOLD VIEW","UPRIGHT DEFAULT"};
    const int x[]={12,268,440,612},w[]={244,160,160,176};
    for(int i=0;i<4;i++) {
        lv_obj_t *b=lv_button_create(screen);
        lv_obj_set_pos(b,x[i],430); lv_obj_set_size(b,w[i],40);
        lv_obj_set_style_bg_color(b,lv_color_hex(i==0?0x126C83:0x173147),0);
        lv_obj_set_style_radius(b,9,0);
        lv_obj_add_event_cb(b,button_event,LV_EVENT_CLICKED,(void *)(intptr_t)i);
        lv_obj_t *text=label(b,names[i],0,0,LV_FONT_DEFAULT,0xECF5FF);
        lv_obj_center(text); if(i==2) hold_label=text;
    }
}

static void update_ui(void)
{
    struct telemetry s=snapshot();
    bool stale=k_uptime_get()-s.sample_ms>150 || s.ret;
    bool mag_live=s.mag.ready && s.mag.samples && !s.mag.ret && k_uptime_get()-s.mag.sample_ms<250;
    char mag_text[80];
    lv_label_set_text(mag_label,mag_live?(s.mag.field_high?"BLOCKED: HIGH FIELD":"NOT CALIBRATED"):"I3C NO DATA");
    lv_obj_set_style_text_color(mag_label,lv_color_hex(mag_live && !s.mag.field_high?0x65E3D2:0xFFD14F),0);
    if(mag_live) snprintf(mag_text,sizeof(mag_text),"Field %.0f uT\nNorth unavailable",
              (double)sqrtf(s.mag.xyz[0]*s.mag.xyz[0]+s.mag.xyz[1]*s.mag.xyz[1]+s.mag.xyz[2]*s.mag.xyz[2]));
    else snprintf(mag_text,sizeof(mag_text),"Error %d / samples %u",s.mag.ret,s.mag.samples);
    lv_label_set_text(mag_note,mag_text);
    lv_label_set_text(status_label,stale?"IMU DATA STALE":s.calibrating?
                      "CALIBRATING - KEEP STILL":held?"VIEW HELD / FUSION LIVE":
                      k_uptime_get()<zero_message_until?"GROUND ZERO SET":
                      s.ground_frame?"KALMAN LIVE / GROUND REFERENCE":"KALMAN LIVE / UPRIGHT FRAME");
    bool baro_live=s.baro_samples && !s.baro_ret && k_uptime_get()-s.baro_ms<500;
    lv_label_set_text(baro_note,baro_live?"Climb estimate / m/s":"BARO STALE / NO DATA");
    if(!baro_live) {
        lv_label_set_text(alt_label,"-- m"); lv_label_set_text(climb_label,"-- m/s");
        lv_label_set_text(pressure_label,"-- hPa\n-- C");
    }
    if(held || stale || !s.samples) return;
    uint32_t begin=k_cycle_get_32();
    float roll=s.roll,pitch=s.pitch;
    horizon_draw(horizon_pixels,roll,pitch);
    raster_crc=crc32_ieee((const uint8_t *)horizon_pixels,sizeof(horizon_pixels));
    lv_obj_invalidate(horizon);
    char buf[100];
    snprintf(buf,sizeof(buf),"BANK %+.1f deg",(double)roll); lv_label_set_text(roll_label,buf);
    snprintf(buf,sizeof(buf),"PITCH %+.1f deg",(double)pitch); lv_label_set_text(pitch_label,buf);
    float heading=cockpit_heading(s.yaw);
    snprintf(buf,sizeof(buf),"%05.1f deg",(double)heading); lv_label_set_text(yaw_label,buf);
    update_heading(heading);
    snprintf(buf,sizeof(buf),"%.2f g",(double)s.g); lv_label_set_text(g_label,buf);
    snprintf(buf,sizeof(buf),"Z %+.1f deg/s",(double)s.gyro[2]);
    lv_label_set_text(gyro_label,buf);
    if(baro_live) {
        snprintf(buf,sizeof(buf),"%+.1f m",(double)s.altitude_m); lv_label_set_text(alt_label,buf);
        snprintf(buf,sizeof(buf),"%+.2f m/s",(double)s.climb_mps); lv_label_set_text(climb_label,buf);
        snprintf(buf,sizeof(buf),"%.1f hPa\n%.1f C",(double)s.pressure_kpa*10,(double)s.temperature);
        lv_label_set_text(pressure_label,buf);
    }
    static int64_t previous; static uint32_t count;
    int64_t now=k_uptime_get();
    if(now-previous>=1000) {
        snprintf(buf,sizeof(buf),"%u Hz / EDUCATIONAL - NOT FOR FLIGHT",(unsigned)((s.samples-count)*1000/(now-previous)));
        lv_label_set_text(rate_label,buf); previous=now; count=s.samples;
    }
    renders++; render_us=k_cyc_to_us_floor32(k_cycle_get_32()-begin);
}

static void status(const char *token)
{
    struct telemetry s=snapshot(); struct ft5406_status touch; struct waveshare_status d;
    ft5406_get_status(&touch); int display_ret=waveshare_get_status(display,&d);
    printk("OK HORIZON STATUS token=%s samples=%u corrections=%u rejected=%u renders=%u "
           "roll10=%d pitch10=%d yaw10=%d g100=%d cal=%u calibrating=%u calibrations=%u held=%u "
           "age_ms=%lld touch=%u touches=%u compass=%s crc=%08x render_us=%u display_ret=%d ret=%d "
           "frame=%s zeros=%u baro_samples=%u baro_errors=%u baro_age_ms=%lld baro_ret=%d "
           "pressure_pa=%d temperature100=%d altitude100=%d climb100=%d reference_pa=%d "
           "convention=pilot_v2 heading10=%u turn10=%d heading_style=rotating_card card_heading10=%u\n",
           token,s.samples,s.corrections,s.rejected,renders,
           (int)(s.roll*10),(int)(s.pitch*10),
           (int)(s.yaw*10),(int)(s.g*100),s.calibrated,s.calibrating,s.calibrations,held,
           (long long)(k_uptime_get()-s.sample_ms),touch.ready,touch.touches,
           s.mag.ready && s.mag.samples && !s.mag.ret && k_uptime_get()-s.mag.sample_ms<250?"live":"unavailable",raster_crc,render_us,
           display_ret,s.ret?s.ret:touch.last_error,s.ground_frame?"ground":"upright",s.zeros,s.baro_samples,s.baro_errors,
           (long long)(k_uptime_get()-s.baro_ms),s.baro_ret,
           (int)(s.pressure_kpa*1000),(int)(s.temperature*100),
           (int)(s.altitude_m*100),(int)(s.climb_mps*100),(int)(s.reference_kpa*1000),
           (unsigned)lroundf(cockpit_heading(s.yaw)*10),(int)(s.gyro[2]*10),
           (unsigned)lroundf(displayed_heading*10));
}
static void compass_status(const char *token)
{
    struct telemetry s=snapshot(); struct magnetometer_status *m=&s.mag;
    printk("OK COMPASS STATUS token=%s ready=%u chip_id=%02x address=%02x samples=%u errors=%u "
           "transfers=%u clock_hz=%u bus=%08x age_ms=%lld x100=%d y100=%d z100=%d temp100=%d field_high=%u ret=%d\n",
           token,m->ready,m->chip_id,m->address,m->samples,m->errors,m->transfers,m->clock_hz,m->bus_status,
           (long long)(k_uptime_get()-m->sample_ms),(int)(m->xyz[0]*100),(int)(m->xyz[1]*100),
           (int)(m->xyz[2]*100),(int)(m->temperature*100),m->field_high,m->ret);
}
static void command(char *text)
{
    char cmd[16],token[40],extra;
    if(sscanf(text,"horizon %15s %39s %c",cmd,token,&extra)!=2) {
        printk("FAIL HORIZON COMMAND\n"); return;
    }
    if(!strcmp(cmd,"compass")) { compass_status(token); return; }
    if(!strcmp(cmd,"zero")) {
        if(!zero_view()) { printk("FAIL HORIZON ZERO_NOT_READY token=%s\n",token); return; }
    }
    else if(!strcmp(cmd,"upright")) upright_view();
    else if(!strcmp(cmd,"calibrate")) calibrate();
    else if(!strcmp(cmd,"hold")) held=true;
    else if(!strcmp(cmd,"live")) held=false;
    else if(strcmp(cmd,"info")) { printk("FAIL HORIZON COMMAND token=%s\n",token); return; }
    lv_label_set_text(hold_label,held?"RESUME":"HOLD VIEW"); status(token);
}

int main(void)
{
    const struct device *uart=DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    if(!device_is_ready(display)||!device_is_ready(imu)) {
        printk("FAIL HORIZON DEVICES display_ready=%u imu_ready=%u\n",
               device_is_ready(display),device_is_ready(imu));
        return 0;
    }
    const struct sensor_value hz={.val1=100};
    int ret=sensor_attr_set(imu,SENSOR_CHAN_ACCEL_XYZ,SENSOR_ATTR_SAMPLING_FREQUENCY,&hz);
    if(!ret) ret=sensor_attr_set(imu,SENSOR_CHAN_GYRO_XYZ,SENSOR_ATTR_SAMPLING_FREQUENCY,&hz);
    if(ret) { printk("FAIL HORIZON IMU_INIT ret=%d\n",ret); return 0; }
    ret=lvgl_init();
    if(ret || !lv_display_get_default()) { printk("FAIL HORIZON LVGL_INIT\n"); return 0; }
    if(ft5406_init()) { printk("FAIL HORIZON TOUCH_INIT\n"); return 0; }
    ft5406_lvgl_register();
    cockpit_upright(&frame);
    k_thread_priority_set(k_current_get(),5);
    k_thread_create(&sensor_thread,sensor_stack,K_THREAD_STACK_SIZEOF(sensor_stack),sample_worker,
                    NULL,NULL,NULL,2,0,K_NO_WAIT);
    state.mag.ret=-EAGAIN;
    k_thread_create(&mag_thread,mag_stack,K_THREAD_STACK_SIZEOF(mag_stack),mag_worker,
                    NULL,NULL,NULL,6,0,K_NO_WAIT);
    state.baro_ret=-EAGAIN;
    k_thread_create(&baro_thread,baro_stack,K_THREAD_STACK_SIZEOF(baro_stack),baro_worker,
                    NULL,NULL,NULL,6,0,K_NO_WAIT);
    horizon_draw(horizon_pixels,0,0); create_ui(); update_ui(); lv_refr_now(NULL);
    if(display_blanking_off(display)) { printk("FAIL HORIZON BACKLIGHT\n"); return 0; }
    printk("OK HORIZON INITIALIZATION_SUCCESS kalman=1 frame=upright convention=pilot_v2 accel=1 gyro=1 compass=initializing baro=initializing\n");
    char line[96]; size_t used=0; bool overflow=false; int64_t next=0;
    while(true) {
        ft5406_poll();
        if(k_uptime_get()>=next) { update_ui(); next=k_uptime_get()+33; }
        unsigned char c;
        while(uart_poll_in(uart,&c)==0) {
            if(c=='\r'||c=='\n') {
                if(overflow) printk("FAIL HORIZON COMMAND_TOO_LONG\n");
                else if(used) { line[used]='\0'; command(line); }
                used=0; overflow=false;
            } else if(used<sizeof(line)-1) line[used++]=c;
            else overflow=true;
        }
        lv_timer_handler(); k_msleep(2);
    }
}
