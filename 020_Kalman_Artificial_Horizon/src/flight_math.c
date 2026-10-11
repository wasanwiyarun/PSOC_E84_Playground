#include "flight_math.h"
#include <math.h>
#include <string.h>
#define RAD 0.017453292519943f

void cockpit_upright(struct cockpit_frame *f)
{
    /* Normal landscape, top edge up: native gravity (0,-1,0).
     * Filter X=-native Z, Y=native X, Z=-native Y. Rotate BEFORE fusion.
     * Pilot-facing signs are converted separately by cockpit_to_pilot(). */
    const float r[9]={0,0,-1, 1,0,0, 0,-1,0};
    memcpy(f->r,r,sizeof(r));
}

void cockpit_apply(const struct cockpit_frame *f, const float native[3], float body[3])
{
    float out[3]={0};
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) out[i]+=f->r[3*i+j]*native[j];
    memcpy(body,out,sizeof(out));
}

void cockpit_to_pilot(const float filter[3], float pilot[3])
{
    /* Change both body/world attitude conventions via conjugation with
     * D=diag(1,-1,-1): D Rz(yaw) Ry(pitch) Rx(bank) D becomes
     * Rz(-yaw) Ry(-pitch) Rx(bank). Body rates transform by D as well.
     * Keep the proper sensor rotation and +Z-gravity filter unchanged. */
    pilot[0]=filter[0]; pilot[1]=-filter[1]; pilot[2]=-filter[2];
}

float cockpit_heading(float yaw)
{
    /* Normalize AFTER rounding so 359.96 does not print as 360.0. */
    float tenth=fmodf(roundf(yaw*10.0f),3600.0f);
    if(tenth<0) tenth+=3600.0f;
    return tenth==0?0:tenth*0.1f;
}

void cockpit_card_point(float heading, float mark, float radius, float xy[2])
{
    float angle=(mark-heading)*RAD;
    xy[0]=radius*sinf(angle); xy[1]=-radius*cosf(angle);
}

void cockpit_zero(struct cockpit_frame *f, float roll, float pitch)
{
    /* R_y(pitch) R_x(roll) maps the current estimated gravity to +Z.
     * Compose rotations, never subtract Euler angles at the display. */
    float cr=cosf(roll*RAD),sr=sinf(roll*RAD),cp=cosf(pitch*RAD),sp=sinf(pitch*RAD);
    float q[9]={cp,sp*sr,sp*cr, 0,cr,-sr, -sp,cp*sr,cp*cr}, out[9]={0};
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) for(int k=0;k<3;k++)
        out[3*i+j]+=q[3*i+k]*f->r[3*k+j];
    memcpy(f->r,out,sizeof(out));
}

static bool valid_pressure(float p) { return isfinite(p) && p>=30.0f && p<=120.0f; }

bool baro_zero(struct baro_filter *s, float p)
{
    if(!valid_pressure(p)) return false;
    s->reference_kpa=p; s->altitude_m=0; s->climb_mps=0; s->initialized=true;
    return true;
}

bool baro_update(struct baro_filter *s, float p, float dt)
{
    if(!valid_pressure(p) || !isfinite(dt) || dt<=0 || dt>60.0f) return false;
    if(!s->initialized) return baro_zero(s,p);
    /* Standard-atmosphere pressure-ratio estimate, not surveyed altitude/AGL.
     * 0.5 s height LPF followed by 1.5 s derivative LPF limits sensor noise. */
    float height=44330.0f*(1.0f-powf(p/s->reference_kpa,0.19029496f));
    /* Following a data gap, preserve ground pressure but do not differentiate
     * across the missing samples (which would invent a climb-rate spike). */
    if(dt>2.0f) { s->altitude_m=height; s->climb_mps=0; return true; }
    float delta=dt/(0.5f+dt)*(height-s->altitude_m);
    s->altitude_m+=delta;
    s->climb_mps+=dt/(1.5f+dt)*(delta/dt-s->climb_mps);
    return true;
}
