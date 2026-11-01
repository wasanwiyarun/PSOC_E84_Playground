#include "motion.h"
#include <math.h>

void motion_screen_gravity(int32_t ax_mg, int32_t ay_mg, float *gx, float *gy)
{
	/* TESA landscape mounting: screen right is +sensor X acceleration;
	 * screen down is -sensor Y acceleration. Both signs in the initial
	 * demo were reversed, so liquid collected against the lifted edge.
	 * Z points out of the screen and must not overpower in-plane gravity. */
	*gx = (float)ax_mg * 0.27f;
	*gy = -(float)ay_mg * 0.27f;

	/* A weak downward preference only while almost flat (< about 5 deg).
	 * Fade it out entirely before an appreciable tilt; never use |Z| as a
	 * permanent downward force, which prevents pouring toward the top. */
	float planar_mg = sqrtf((float)ax_mg * ax_mg + (float)ay_mg * ay_mg);
	if (planar_mg < 80.0f)
		*gy += 24.0f * (1.0f - planar_mg / 80.0f);
}
