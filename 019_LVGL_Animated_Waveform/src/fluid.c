#include "fluid.h"
#include <math.h>
#include <string.h>

/* Small 2-D double-density relaxation solver. Units are bitmap pixels and
 * seconds. The same engine is compiled into the firmware and host tests. */
#define RADIUS 7.0f
#define CELL 7
#define NX ((FLUID_WIDTH + CELL - 1) / CELL)
#define NY ((FLUID_HEIGHT + CELL - 1) / CELL)
#define DT (1.0f / 60.0f)
struct particle { float x, y, ox, oy, vx, vy; };
static struct particle p[FLUID_PARTICLES];
static int16_t heads[NX * NY], next[FLUID_PARTICLES];
static uint16_t density_map[FLUID_WIDTH * FLUID_HEIGHT];
static unsigned wet_pixels;
/* Accumulate milliseconds * 60 to avoid rounding drift at 60 physics Hz. */
static uint32_t time_units;

static float clampf(float x, float lo, float hi)
{ return x < lo ? lo : (x > hi ? hi : x); }

void fluid_reset(void)
{
	time_units = 0;
	for (int i = 0; i < FLUID_PARTICLES; ++i) {
		p[i] = (struct particle){ .x = 22.0f + (i % 48) * 2.8f,
			.y = 40.0f + (i / 48) * 2.8f };
	}
}

static void grid_build(void)
{
	memset(heads, -1, sizeof(heads));
	for (int i = 0; i < FLUID_PARTICLES; ++i) {
		int x = (int)clampf(p[i].x / CELL, 0, NX - 1);
		int y = (int)clampf(p[i].y / CELL, 0, NY - 1);
		int cell = y * NX + x;
		next[i] = heads[cell]; heads[cell] = i;
	}
}

static void substep(float gx, float gy)
{
	for (int i = 0; i < FLUID_PARTICLES; ++i) {
		p[i].ox = p[i].x; p[i].oy = p[i].y;
		p[i].vx = clampf((p[i].vx + gx * DT) * 0.998f, -220, 220);
		p[i].vy = clampf((p[i].vy + gy * DT) * 0.998f, -220, 220);
		p[i].x = clampf(p[i].x + p[i].vx * DT, 2, FLUID_WIDTH - 3);
		p[i].y = clampf(p[i].y + p[i].vy * DT, 2, FLUID_HEIGHT - 3);
	}
	grid_build();
	for (int i = 0; i < FLUID_PARTICLES; ++i) {
		/* Freeze neighbour coordinates during this particle's density solve. */
		int neighbours[96], count = 0;
		float qs[96], dxs[96], dys[96];
		float rho = 0, near_rho = 0;
		int cx = (int)(p[i].x / CELL), cy = (int)(p[i].y / CELL);
		for (int yy = cy - 1; yy <= cy + 1; ++yy) {
			if (yy < 0 || yy >= NY) continue;
			for (int xx = cx - 1; xx <= cx + 1; ++xx) {
				if (xx < 0 || xx >= NX) continue;
				for (int j = heads[yy * NX + xx]; j >= 0; j = next[j]) {
					if (j == i) continue;
					float dx = p[j].x - p[i].x, dy = p[j].y - p[i].y;
					float r2 = dx * dx + dy * dy;
					if (r2 >= RADIUS * RADIUS || r2 < 0.00001f) continue;
					float r = sqrtf(r2), q = 1.0f - r / RADIUS;
					rho += q * q; near_rho += q * q * q;
					if (count < 96) {
						neighbours[count] = j; qs[count] = q;
						dxs[count] = dx / r; dys[count++] = dy / r;
					}
				}
			}
		}
		float pressure = clampf(350.0f * (rho - 4.0f), -120, 2500);
		float near_pressure = 800.0f * near_rho;
		float move_x = 0, move_y = 0;
		for (int n = 0; n < count; ++n) {
			float q = qs[n];
			float d = clampf(DT * DT * (pressure * q + near_pressure * q * q) * 0.5f,
				-0.08f, 0.5f);
			float dx = d * dxs[n], dy = d * dys[n];
			p[neighbours[n]].x += dx; p[neighbours[n]].y += dy;
			move_x -= dx; move_y -= dy;
		}
		p[i].x += move_x; p[i].y += move_y;
	}
	for (int i = 0; i < FLUID_PARTICLES; ++i) {
		p[i].x = clampf(p[i].x, 2, FLUID_WIDTH - 3);
		p[i].y = clampf(p[i].y, 2, FLUID_HEIGHT - 3);
		p[i].vx = (p[i].x - p[i].ox) / DT;
		p[i].vy = (p[i].y - p[i].oy) / DT;
	}
}

void fluid_step(float gx, float gy)
{
	gx = clampf(gx, -280, 280); gy = clampf(gy, -280, 280);
	substep(gx, gy); substep(gx, gy);
}

unsigned fluid_advance(float gx, float gy, uint32_t elapsed_ms)
{
	/* Keep the stable 1/60 s timestep, but run enough substeps for the time
	 * spent refreshing the display. Bound catch-up after pauses to six steps. */
	if (elapsed_ms > 100) elapsed_ms = 100;
	time_units += elapsed_ms * 60;
	unsigned steps = time_units / 1000;
	time_units %= 1000;
	gx = clampf(gx, -280, 280); gy = clampf(gy, -280, 280);
	for (unsigned i = 0; i < steps; ++i) substep(gx, gy);
	return steps;
}

/* Rasterize a smooth density field so neighbouring particles form one liquid
 * body, while detached particles remain visible as droplets. No LVGL objects
 * per particle and no pre-drawn waterline. */
void fluid_render(uint16_t *pixels)
{
	memset(density_map, 0, sizeof(density_map));
	for (int i = 0; i < FLUID_PARTICLES; ++i) {
		int cx = (int)p[i].x, cy = (int)p[i].y;
		for (int y = cy - 4; y <= cy + 4; ++y) {
			if (y < 0 || y >= FLUID_HEIGHT) continue;
			for (int x = cx - 4; x <= cx + 4; ++x) {
				if (x < 0 || x >= FLUID_WIDTH) continue;
				float dx = x - p[i].x, dy = y - p[i].y;
				float w = 1.0f - (dx * dx + dy * dy) / 16.0f;
				if (w > 0) density_map[y * FLUID_WIDTH + x] += (uint16_t)(w * w * 100);
			}
		}
	}
	wet_pixels = 0;
	for (int y = 0; y < FLUID_HEIGHT; ++y) {
		for (int x = 0; x < FLUID_WIDTH; ++x) {
			int k = y * FLUID_WIDTH + x;
			uint16_t d = density_map[k];
			pixels[k] = 0x0021; /* almost black */
			if (d < 45) continue;
			wet_pixels++;
			/* Thin pale foam edge, cyan shallow water, deep blue interior. */
			if (d < 90 || (y > 0 && density_map[k - FLUID_WIDTH] < 45))
				pixels[k] = 0xbfff;
			else pixels[k] = (uint16_t)(((24 + (FLUID_HEIGHT - y) / 3) << 5) | 29);
		}
	}
}

int fluid_valid(void)
{
	for (int i = 0; i < FLUID_PARTICLES; ++i)
		if (!isfinite(p[i].x) || !isfinite(p[i].y) || !isfinite(p[i].vx) ||
		    !isfinite(p[i].vy) || p[i].x < 2 || p[i].x > FLUID_WIDTH - 3 ||
		    p[i].y < 2 || p[i].y > FLUID_HEIGHT - 3) return 0;
	return 1;
}
int fluid_center_x(void)
{ float v = 0; for (int i = 0; i < FLUID_PARTICLES; ++i) v += p[i].x; return (int)(v / FLUID_PARTICLES); }
int fluid_center_y(void)
{ float v = 0; for (int i = 0; i < FLUID_PARTICLES; ++i) v += p[i].y; return (int)(v / FLUID_PARTICLES); }
unsigned fluid_pixels(void) { return wet_pixels; }
