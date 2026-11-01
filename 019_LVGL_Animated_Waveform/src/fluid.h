#ifndef WATER_FLUID_H
#define WATER_FLUID_H
#include <stdint.h>
#define FLUID_WIDTH 180
#define FLUID_HEIGHT 78
#define FLUID_PARTICLES 480
void fluid_reset(void);
void fluid_step(float gx, float gy);
unsigned fluid_advance(float gx, float gy, uint32_t elapsed_ms);
void fluid_render(uint16_t *pixels);
int fluid_valid(void);
int fluid_center_x(void);
int fluid_center_y(void);
unsigned fluid_pixels(void);
#endif
