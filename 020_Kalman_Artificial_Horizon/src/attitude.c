#include "attitude.h"
#include <math.h>
#include <string.h>
#define DEG 57.2957795131f
#define RAD (1.0f / DEG)

float attitude_wrap(float a)
{
    a = fmodf(a + 180.0f, 360.0f);
    if (a < 0) a += 360.0f;
    return a - 180.0f;
}

void attitude_reset(struct attitude *s) { memset(s, 0, sizeof(*s)); }

static void kalman(struct angle_kalman *k, float rate, float measured, float dt, bool correct)
{
    k->angle = attitude_wrap(k->angle + dt * (rate - k->bias));
    /* State [angle, gyro bias], covariance propagated by F=[1,-dt;0,1]. */
    k->p00 += dt * (dt * k->p11 - k->p01 - k->p10 + 0.02f);
    k->p01 -= dt * k->p11;
    k->p10 -= dt * k->p11;
    k->p11 += 0.003f * dt;
    if (!correct) return;
    float innovation = attitude_wrap(measured - k->angle);
    float variance = k->p00 + 2.0f;
    float k0 = k->p00 / variance, k1 = k->p10 / variance;
    k->angle = attitude_wrap(k->angle + k0 * innovation);
    k->bias += k1 * innovation;
    float p00 = k->p00, p01 = k->p01;
    k->p00 -= k0 * p00; k->p01 -= k0 * p01;
    k->p10 -= k1 * p00; k->p11 -= k1 * p01;
}

bool attitude_update(struct attitude *s, const float a[3], const float w[3], float dt)
{
    for (int i = 0; i < 3; ++i)
        if (!isfinite(a[i]) || !isfinite(w[i])) return false;
    if (!isfinite(dt) || dt <= 0 || dt > 0.1f) return false;
    float norm = sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
    if (norm < 0.01f) return false;
    float roll_acc = atan2f(a[1], a[2]) * DEG;
    float pitch_acc = atan2f(-a[0], sqrtf(a[1]*a[1] + a[2]*a[2])) * DEG;
    s->g = norm;
    s->accel_used = norm > 0.85f && norm < 1.15f;
    if (!s->initialized) {
        if (!s->accel_used) return false;
        s->roll.angle = roll_acc; s->pitch.angle = pitch_acc;
        s->roll.p00 = s->pitch.p00 = 1.0f;
        s->roll.p11 = s->pitch.p11 = 1.0f;
        s->initialized = true;
        return true;
    }
    float phi = s->roll.angle * RAD;
    /* Euler rates are singular at pitch +/-90: limit rate conversion near
     * vertical; accelerometer correction remains available. */
    float theta = fmaxf(-80.0f, fminf(80.0f, s->pitch.angle)) * RAD;
    float sp = sinf(phi), cp = cosf(phi), ct = cosf(theta);
    float cross = w[1] * sp + w[2] * cp;
    float roll_rate = (w[0] + tanf(theta) * cross) * DEG;
    float pitch_rate = (w[1] * cp - w[2] * sp) * DEG;
    kalman(&s->roll, roll_rate, roll_acc, dt, s->accel_used);
    kalman(&s->pitch, pitch_rate, pitch_acc, dt, s->accel_used);
    s->yaw = attitude_wrap(s->yaw + dt * cross / ct * DEG);
    return isfinite(s->roll.angle) && isfinite(s->pitch.angle) && isfinite(s->yaw);
}
