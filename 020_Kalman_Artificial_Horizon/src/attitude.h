#ifndef ATTITUDE_H
#define ATTITUDE_H
#include <stdbool.h>
struct angle_kalman { float angle, bias, p00, p01, p10, p11; };
struct attitude {
    struct angle_kalman roll, pitch;
    float yaw, g;
    bool initialized, accel_used;
};
void attitude_reset(struct attitude *s);
/* Acceleration in g, gyro in rad/s, dt in seconds. Output angles in degrees.
 * Inputs must already share the chosen right-handed body frame:
 * X-roll/Y-pitch/Z-yaw, with resting specific force +Z at level. */
bool attitude_update(struct attitude *s, const float a[3], const float w[3], float dt);
float attitude_wrap(float angle);
#endif
