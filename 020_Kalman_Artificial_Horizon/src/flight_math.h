#ifndef FLIGHT_MATH_H
#define FLIGHT_MATH_H
#include <stdbool.h>

/* Proper rotation (determinant +1), applied equally to acceleration and gyro. */
struct cockpit_frame { float r[9]; };
void cockpit_upright(struct cockpit_frame *f);
void cockpit_apply(const struct cockpit_frame *f, const float native[3], float body[3]);
void cockpit_zero(struct cockpit_frame *f, float roll_deg, float pitch_deg);
/* Filter-to-pilot convention: bank unchanged, pitch/yaw reversed. Applies
 * to Euler angles and body angular rates; NOT to accelerometer vectors.
 * Ground zero must continue using the original filter angles. */
void cockpit_to_pilot(const float filter[3], float pilot[3]);
/* Rounded display heading, always 0.0..359.9 degrees (including at wrap). */
float cockpit_heading(float yaw_deg);
/* Heading-up card coordinates relative to its centre. The card rotates
 * opposite the heading; a mark equal to heading is always at twelve o'clock. */
void cockpit_card_point(float heading_deg, float mark_deg, float radius, float xy[2]);

struct baro_filter {
    float reference_kpa, altitude_m, climb_mps;
    bool initialized;
};
bool baro_update(struct baro_filter *s, float pressure_kpa, float dt);
bool baro_zero(struct baro_filter *s, float pressure_kpa);
#endif
