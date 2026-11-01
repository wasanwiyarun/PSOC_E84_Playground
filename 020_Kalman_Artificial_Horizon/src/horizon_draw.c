#include "horizon_draw.h"
#include <math.h>
#define C (HORIZON_SIZE / 2)
#define R (C - 6)
#define RAD 0.01745329252f
static uint16_t *image;
static uint16_t palette[1025];
static int palette_ready;
static uint16_t rgb(int r, int g, int b)
{ return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
static void pixel(int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= HORIZON_SIZE || y >= HORIZON_SIZE) return;
    if ((x-C)*(x-C)+(y-C)*(y-C) > (R-2)*(R-2)) return;
    image[y * HORIZON_SIZE + x] = color;
}
static void line(int x0, int y0, int x1, int y1, uint16_t color, int thick)
{
    int dx = x1 > x0 ? x1-x0 : x0-x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0-y1 : y1-y0, sy = y0 < y1 ? 1 : -1;
    int err = dx+dy;
    for (;;) {
        for (int y = 0; y < thick; ++y)
            for (int x = 0; x < thick; ++x) pixel(x0+x, y0+y, color);
        if (x0 == x1 && y0 == y1) break;
        int e = 2*err;
        if (e >= dy) { err += dy; x0 += sx; }
        if (e <= dx) { err += dx; y0 += sy; }
    }
}
static const unsigned char digits[10][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}
};
static void number(int x, int y, int n)
{
    int nums[2] = {n/10,n%10};
    for (int d=0; d<2; ++d)
        for (int yy=0; yy<5; ++yy)
            for (int xx=0; xx<3; ++xx)
                if (digits[nums[d]][yy] & (4 >> xx))
                    for(int j=0;j<2;j++) for(int i=0;i<2;i++) pixel(x+d*8+xx*2+i,y+yy*2+j,0xffff);
}
void horizon_draw(uint16_t *pixels, float bank, float pitch)
{
    image = pixels;
    if (!palette_ready) {
        for (int i=0;i<1025;i++) {
            int v=i-512;
            palette[i]=v<0 ? rgb(26,100+(int)fminf(55,-v/3.0f),190+(int)fminf(50,-v/4.0f)) :
                             rgb(126-(int)fminf(50,v/4.0f),75-(int)fminf(30,v/6.0f),42);
        }
        palette_ready=1;
    }
    bank = fmaxf(-180, fminf(180, bank)); pitch = fmaxf(-70, fminf(70, pitch));
    float sn = sinf(bank*RAD), cs = cosf(bank*RAD), offset = pitch*2.5f;
    uint16_t rim = rgb(59,83,106), bg = rgb(12,20,31);
    for (int y=0; y<HORIZON_SIZE; ++y) {
        float vertical = (y-C)*cs-offset-C*sn;
        for (int x=0; x<HORIZON_SIZE; ++x,vertical+=sn) {
            int r2 = (x-C)*(x-C)+(y-C)*(y-C);
            uint16_t color = bg;
            if (r2 <= R*R) {
                if (r2 >= (R-3)*(R-3)) color = rim;
                else if (fabsf(vertical) < 1.1f) color=0xffff;
                else {
                    int index=(int)vertical+512;
                    if(index<0) index=0;
                    if(index>1024) index=1024;
                    color=palette[index];
                }
            }
            pixels[y*HORIZON_SIZE+x]=color;
        }
    }
    /* Pitch ladder turns with the horizon, beneath the fixed aircraft. */
    for (int deg=-40; deg<=40; deg+=10) {
        if (!deg) continue;
        float y=offset-deg*2.5f;
        int half=deg%20 == 0 ? 48 : 28;
        int x0=(int)(C-half*cs+y*sn), y0=(int)(C+half*sn+y*cs);
        int x1=(int)(C+half*cs+y*sn), y1=(int)(C-half*sn+y*cs);
        line(x0,y0,x1,y1,0xffff,1);
        if (fabsf(y)<110) number(x1+6,y1-5,deg<0?-deg:deg);
    }
    for(int deg=-60;deg<=60;deg+=10) {
        float a=deg*RAD;
        int len=deg%30==0?14:7;
        line(C+(int)(sinf(a)*(R-10)),C-(int)(cosf(a)*(R-10)),
             C+(int)(sinf(a)*(R-10-len)),C-(int)(cosf(a)*(R-10-len)),0xffff,2);
    }
    uint16_t gold=rgb(255,209,79);
    float a=-bank*RAD;
    int bx=C+(int)(sinf(a)*(R-34)), by=C-(int)(cosf(a)*(R-34));
    line(bx-5,by+5,bx,by-4,gold,2); line(bx,by-4,bx+5,by+5,gold,2);
    line(C-85,C,C-26,C,gold,3); line(C-26,C,C-26,C+10,gold,3);
    line(C+26,C,C+85,C,gold,3); line(C+26,C,C+26,C+10,gold,3);
    line(C-5,C,C+5,C,gold,3);
}
