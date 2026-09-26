#ifndef CAMERA_H
#define CAMERA_H

#include "../types.h"
#include "droid.h"

#define CAMERA_SHAKE_LONG (20.0f * (1.0f / 30.0f))
#define CAMERA_SHAKE_SHORT (2.0f * (1.0f / 30.0f))

typedef struct camera_t {
	vec3_t position;
	vec3_t velocity;
	vec3_t angle;
	vec3_t angular_velocity;
	vec3_t last_position;
	vec3_t real_velocity;
	section_t *section;
	bool has_initial_section;
	float update_timer;
	void (*update_func)(struct camera_t *, ship_t *, droid_t *);
	vec2_t shake;
	float shake_timer;
} camera_t;

// A cone around the view frustum, for cheap culling of bounding spheres. It's
// independent of the camera roll.
typedef struct {
	vec3_t position;
	vec3_t forward;
	float sin_angle;
	float cos_angle;
	float far;
} camera_view_cone_t;

camera_view_cone_t camera_view_cone(camera_t *camera);

static inline bool camera_view_cone_has_sphere(camera_view_cone_t *cone, vec3_t center, float radius) {
	vec3_t d = vec3_sub(center, cone->position);
	float dist_sq = vec3_dot(d, d);
	float far = cone->far + radius;
	if (dist_sq > far * far) {
		return false;
	}

	// Distance of the sphere's center to the surface of the cone
	float along = vec3_dot(d, cone->forward);
	float perp = sqrtf(max(0.0f, dist_sq - along * along));
	return (perp * cone->cos_angle - along * cone->sin_angle) < radius;
}

void camera_init(camera_t *camera, section_t *section);
vec3_t camera_forward(camera_t *camera);
void camera_update(camera_t *camera, ship_t *ship, droid_t *droid);
void camera_update_race_external(camera_t *, ship_t *camShip, droid_t *);
void camera_update_race_internal(camera_t *, ship_t *camShip, droid_t *);
void camera_update_race_intro(camera_t *, ship_t *camShip, droid_t *);
void camera_update_attract_circle(camera_t *, ship_t *camShip, droid_t *);
void camera_update_attract_internal(camera_t *, ship_t *camShip, droid_t *);
void camera_update_static_follow(camera_t *, ship_t *camShip, droid_t *);
void camera_update_attract_random(camera_t *, ship_t *camShip, droid_t *);
void camera_update_rescue(camera_t *, ship_t *camShip, droid_t *);
void camera_set_shake(camera_t *, float duration);
void camera_update_shake(camera_t *);

#endif
