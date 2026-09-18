#include "../mem.h"
#include "../utils.h"
#include "../system.h"

#include "track.h"
#include "ship.h"
#include "weapon.h"
#include "object.h"
#include "game.h"
#include "race.h"
#include "image.h"
#include "particle.h"
#include "camera.h"

typedef struct weapon_t {
	float timer;
	ship_t *owner;
	ship_t *target;
	section_t *section;
	Object *model;
	bool active;

	int16_t trail_particle;
	int16_t track_hit_particle;
	int16_t ship_hit_particle;
	float trail_spawn_timer;

	int16_t type;
	vec3_t acceleration;
	vec3_t velocity;
	vec3_t position;
	vec3_t angle;
	float drag;

	void (*update_func)(struct weapon_t *);
} weapon_t;


weapon_t *weapons;
int weapons_active = 0;

struct {
	uint16_t reticle;
	Object *rocket;
	Object *mine;
	Object *missile;
	Object *shield;
	Object *shield_internal;
	Object *ebolt;
} weapon_assets;

void weapon_update_wait_for_delay(weapon_t *self);

void weapon_fire_mine(ship_t *ship);
void weapon_update_mine_wait_for_release(weapon_t *self);
void weapon_update_mine(weapon_t *self);
void weapon_update_mine_lights(weapon_t *self, int index);

void weapon_fire_missile(ship_t *ship);
void weapon_update_missile(weapon_t *self);

void weapon_fire_rocket(ship_t *ship);
void weapon_update_rocket(weapon_t *self);

void weapon_fire_ebolt(ship_t *ship);
void weapon_update_ebolt(weapon_t *self);

void weapon_fire_shield(ship_t *ship);
void weapon_update_shield(weapon_t *self);

void weapon_fire_turbo(ship_t *ship);

void invert_shield_polys(Object *shield);

void weapons_load(void) {
	weapons = mem_bump(sizeof(weapon_t) * WEAPONS_MAX);
	weapon_assets.reticle = image_get_texture("wipeout/textures/target2.tim");

	texture_list_t weapon_textures = image_get_compressed_textures("wipeout/common/mine.cmp");
	weapon_assets.rocket          = objects_load("wipeout/common/rock.prm", weapon_textures);
	weapon_assets.mine            = objects_load("wipeout/common/mine.prm", weapon_textures);
	weapon_assets.missile         = objects_load("wipeout/common/miss.prm", weapon_textures);
	weapon_assets.shield          = objects_load("wipeout/common/shld.prm", weapon_textures);
	weapon_assets.shield_internal = objects_load("wipeout/common/shld.prm", weapon_textures);
	weapon_assets.ebolt           = objects_load("wipeout/common/ebolt.prm", weapon_textures);

	// Invert shield polys for internal view
	Prm poly = {.primitive = weapon_assets.shield_internal->primitives};
	int primitives_len = weapon_assets.shield_internal->primitives_len;
	for (int k = 0; k < primitives_len; k++) {
		switch (poly.primitive->type) {
		case PRM_TYPE_G3 :
			swap(poly.g3->coords[0], poly.g3->coords[2]);
			poly.g3 += 1;
			break;

		case PRM_TYPE_G4 :
			swap(poly.g4->coords[0], poly.g4->coords[3]);
			poly.g4 += 1;
			break;
		}
	}

	weapons_init();
}

// Explosion effects: a bright flash that swells and fades, and an expanding
// shockwave ring. Drawn additively in weapons_draw().
#define EXPLOSIONS_MAX 8
#define EXPLOSION_DURATION 0.55

typedef struct {
	vec3_t pos;
	float timer;
	rgba_t color;
} explosion_fx_t;

static explosion_fx_t explosions[EXPLOSIONS_MAX];

static void explosion_add(vec3_t pos, int particle_type) {
	rgba_t color;
	switch (particle_type) {
	case PARTICLE_TYPE_FIRE: color = rgba(128, 80, 30, 255); break;
	case PARTICLE_TYPE_FIRE_WHITE: color = rgba(128, 118, 100, 255); break;
	case PARTICLE_TYPE_EBOLT:
	case PARTICLE_TYPE_GREENY: color = rgba(60, 128, 70, 255); break;
	default: color = rgba(128, 128, 128, 255); break;
	}
	int slot = 0;
	for (int i = 0; i < EXPLOSIONS_MAX; i++) {
		if (explosions[i].timer < explosions[slot].timer) {
			slot = i;
		}
	}
	explosions[slot] = (explosion_fx_t){.pos = pos, .timer = EXPLOSION_DURATION, .color = color};
}

static void explosions_update(void) {
	for (int i = 0; i < EXPLOSIONS_MAX; i++) {
		if (explosions[i].timer > 0) {
			explosions[i].timer -= system_tick();
		}
	}
}

static void explosions_draw(void) {
	uint16_t flare = ship_exhaust_flare_texture();
	uint16_t ring = ship_ring_texture();
	for (int i = 0; i < EXPLOSIONS_MAX; i++) {
		explosion_fx_t *e = &explosions[i];
		if (e->timer <= 0) {
			continue;
		}
		float t = 1.0 - e->timer / EXPLOSION_DURATION; // 0 -> 1

		// Flash: big right away, fading out fast
		float flash_fade = (1.0 - t) * (1.0 - t);
		int flash_size = 500 + 700 * t;
		rgba_t flash = e->color;
		flash.a = 255 * flash_fade;
		render_push_sprite(e->pos, vec2i(flash_size, flash_size), flash, flare);
		rgba_t core = rgba(128, 128, 128, 200 * flash_fade);
		render_push_sprite(e->pos, vec2i(flash_size * 0.4, flash_size * 0.4), core, flare);

		// Shockwave: grows fast, thins out
		float ring_t = sqrtf(t);
		int ring_size = 300 + 2400 * ring_t;
		rgba_t ring_color = e->color;
		ring_color.a = 220 * (1.0 - t);
		render_push_sprite(e->pos, vec2i(ring_size, ring_size), ring_color, ring);
	}
}

void weapons_init(void) {
	weapons_active = 0;
	for (int i = 0; i < EXPLOSIONS_MAX; i++) {
		explosions[i].timer = 0;
	}
}

weapon_t *weapon_init(ship_t *ship) {
	if (weapons_active == WEAPONS_MAX) {
		return NULL;
	}

	weapon_t *weapon = &weapons[weapons_active++];
	weapon->timer = 0;
	weapon->owner = ship;
	weapon->section = ship->section;
	weapon->position = ship->position;
	weapon->angle = ship->angle;	
	weapon->acceleration = vec3(0, 0, 0);
	weapon->velocity = vec3(0, 0, 0);
	weapon->acceleration = vec3(0, 0, 0);
	weapon->target = NULL;
	weapon->model = NULL;
	weapon->active = true;
	weapon->trail_particle = PARTICLE_TYPE_NONE;
	weapon->track_hit_particle = PARTICLE_TYPE_NONE;
	weapon->ship_hit_particle = PARTICLE_TYPE_NONE;
	weapon->trail_spawn_timer = 0;
	weapon->drag = 0;
	return weapon;
}

void weapons_fire(ship_t *ship, int weapon_type) {
	switch (weapon_type) {
		case WEAPON_TYPE_MINE:      weapon_fire_mine(ship); break;
		case WEAPON_TYPE_MISSILE:   weapon_fire_missile(ship); break;
		case WEAPON_TYPE_ROCKET:    weapon_fire_rocket(ship); break;
		case WEAPON_TYPE_EBOLT:     weapon_fire_ebolt(ship); break;
		case WEAPON_TYPE_SHIELD:    weapon_fire_shield(ship); break;
		case WEAPON_TYPE_TURBO:     weapon_fire_turbo(ship); break;
		default: die("Invalid weapon type %d", weapon_type);
	}
	ship->weapon_type = WEAPON_TYPE_NONE;
}

void weapons_fire_delayed(ship_t *ship, int weapon_type) {
	weapon_t *weapon = weapon_init(ship);
	if (!weapon) {
		return;
	}
	weapon->type = weapon_type;
	weapon->timer = WEAPON_AI_DELAY;
	weapon->update_func = weapon_update_wait_for_delay;
}

bool weapon_collides_with_track(weapon_t *self);

void weapons_update(void) {
	explosions_update();
	for (int i = 0; i < weapons_active; i++) {
		weapon_t *weapon = &weapons[i];
		
		weapon->timer -= system_tick();
		(weapon->update_func)(weapon);

		// Handle projectiles
		if (weapon->acceleration.x != 0 || weapon->acceleration.z != 0) {
			weapon->velocity = vec3_add(weapon->velocity, vec3_mulf(weapon->acceleration, 30 * system_tick()));
			weapon->velocity = vec3_sub(weapon->velocity, vec3_mulf(weapon->velocity, weapon->drag * 30 * system_tick()));
			weapon->position = vec3_add(weapon->position, vec3_mulf(weapon->velocity, 30 * system_tick()));

			// Move along track normal
			track_face_t *face = track_section_get_base_face(weapon->section);
			vec3_t face_point = face->tris[0].vertices[0].pos;
			vec3_t face_normal = face->normal;
			float height = vec3_distance_to_plane(weapon->position, face_point, face_normal);

			if (height < 2000) {
				weapon->position = vec3_add(weapon->position, vec3_mulf(face_normal, (200 - height) * 30 * system_tick()));
			}

			// Trail
			if (weapon->trail_particle != PARTICLE_TYPE_NONE) {
				weapon->trail_spawn_timer += system_tick();
				while (weapon->trail_spawn_timer > 0) {
					vec3_t pos = vec3_sub(weapon->position, vec3_mulf(weapon->velocity, 30 * system_tick() * weapon->trail_spawn_timer));
					vec3_t velocity = vec3_rand(128);
					particles_spawn(pos, weapon->trail_particle, velocity, 128);
					weapon->trail_spawn_timer -= WEAPON_PARTICLE_SPAWN_RATE;
				}
			}

			// Track collision
			weapon->section = track_nearest_section(weapon->position, vec3(1,1,1), weapon->section, NULL);
			if (weapon_collides_with_track(weapon)) {
				for (int p = 0; p < 32; p++) {
					vec3_t velocity = vec3_rand(512);
					particles_spawn(weapon->position, weapon->track_hit_particle, velocity, 256);
				}
				// The weapon may already have passed through the wall a bit; put
				// the light back in front of it, or the wall would be on the
				// unlit side
				vec3_t back = vec3_len(weapon->velocity) > 0.001
					? vec3_mulf(vec3_normalize(weapon->velocity), -600)
					: vec3(0, 0, 0);
				race_add_flash_light(vec3_add(weapon->position, back), weapon->track_hit_particle);
				explosion_add(vec3_add(weapon->position, back), weapon->track_hit_particle);
				sfx_play_at(SFX_EXPLOSION_2, weapon->position, vec3(0,0,0), 1);
				weapon->active = false;
			}
		}

		// If this weapon is released, we have to rewind one step
		if (!weapon->active) {
			weapons[i--] = weapons[--weapons_active];
			continue;
		}
	}
}

static rgba_t weapon_shield_vertex_color(weapon_t *self, mat4_t *mat, vec3_t vertex) {
	// Fresnel like rim: bright where the bubble's surface is seen at a grazing
	// angle, nearly invisible where we look straight through it
	vec3_t world = vec3_transform(vertex, mat);
	vec3_t normal = vec3_sub(world, self->position);
	vec3_t view = vec3_sub(g.camera->position, world);
	float len = vec3_len(normal) * vec3_len(view);
	float facing = len > 0.001 ? fabsf(vec3_dot(normal, view) / len) : 1.0;
	float rim = 1.0 - clamp(facing, 0.0, 1.0);
	rim = rim * rim;

	// Energy waves, running over the bubble from front to back
	float wave = sinf(self->timer * 9.0 + vertex.z * 0.012 + vertex.y * 0.02) * 0.5 + 0.5;
	wave = wave * wave;

	// Flicker when the shield is about to run out
	float flicker = self->timer < 1.0 ? (sinf(self->timer * 40.0) * 0.5 + 0.5) : 1.0;

	float alpha = (14.0 + rim * 170.0 + wave * (20.0 + rim * 60.0)) * flicker;
	return rgba(
		24 + wave * 72,
		72 + wave * 56,
		220,
		clamp(alpha, 0.0, 255.0)
	);
}

static void weapon_shield_set_colors(weapon_t *self, mat4_t *mat) {
	Prm poly = {.primitive = self->model->primitives};
	int primitives_len = self->model->primitives_len;
	vec3_t *vertices = self->model->vertices;

	for (int k = 0; k < primitives_len; k++) {
		switch (poly.primitive->type) {
		case PRM_TYPE_G3:
			for (int v = 0; v < 3; v++) {
				poly.g3->color[v] = weapon_shield_vertex_color(self, mat, vertices[poly.g3->coords[v]]);
			}
			poly.g3 += 1;
			break;

		case PRM_TYPE_G4:
			for (int v = 0; v < 4; v++) {
				poly.g4->color[v] = weapon_shield_vertex_color(self, mat, vertices[poly.g4->coords[v]]);
			}
			poly.g4 += 1;
			break;
		}
	}
}

void weapons_draw(void) {
	mat4_t mat = mat4_identity();
	for (int i = 0; i < weapons_active; i++) {
		weapon_t *weapon = &weapons[i];
		if (weapon->model) {
			mat4_set_translation(&mat, weapon->position);
			mat4_set_yaw_pitch_roll(&mat, weapon->angle);
			if (weapon->model == weapon_assets.mine) {
				weapon_update_mine_lights(weapon, i);
			}

			if (weapon->update_func == weapon_update_shield) {
				// The internal view shield is only for the player that sits in it
				bool internal = (
					weapon->owner->player == g.view_player &&
					flags_is(weapon->owner->flags, SHIP_VIEW_INTERNAL)
				);
				weapon->model = internal ? weapon_assets.shield_internal : weapon_assets.shield;
				weapon->position = internal ? ship_cockpit(weapon->owner) : weapon->owner->position;
				mat4_set_translation(&mat, weapon->position);

				// Energy bubble: additive, double sided and without writing to the
				// depth buffer, so that it never hides anything
				weapon_shield_set_colors(weapon, &mat);
				render_set_blend_mode(RENDER_BLEND_LIGHTER);
				render_set_depth_write(false);
				render_set_cull_backface(false);
				object_draw(weapon->model, &mat);
				render_set_cull_backface(true);
				render_set_depth_write(true);
				render_set_blend_mode(RENDER_BLEND_NORMAL);
				continue;
			}
			object_draw(weapon->model, &mat);
		}
	}

	// Flares on the projectiles: an orange exhaust on rockets and missiles, a
	// crackling blue-white core on the e-bolt
	render_set_model_mat(&mat4_identity());
	render_set_material(RENDER_MATERIAL_UNLIT);
	render_set_blend_mode(RENDER_BLEND_LIGHTER);
	render_set_depth_write(false);
	render_set_depth_offset(-32.0);
	uint16_t flare = ship_exhaust_flare_texture();

	for (int i = 0; i < weapons_active; i++) {
		weapon_t *weapon = &weapons[i];
		if (!weapon->model) {
			continue;
		}
		if (weapon->model == weapon_assets.rocket || weapon->model == weapon_assets.missile) {
			float speed = vec3_len(weapon->velocity);
			vec3_t back = speed > 0.001 ? vec3_mulf(weapon->velocity, -110.0 / speed) : vec3(0, 0, 0);
			vec3_t tail = vec3_add(weapon->position, back);
			int size = 200 * rand_float(0.85, 1.15);
			render_push_sprite(tail, vec2i(size, size), rgba(128, 60, 16, 255), flare);
			render_push_sprite(tail, vec2i(size * 0.45, size * 0.45), rgba(128, 110, 70, 255), flare);
		}
		else if (weapon->model == weapon_assets.ebolt) {
			int size = 260 * rand_float(0.7, 1.3);
			render_push_sprite(weapon->position, vec2i(size, size), rgba(40, 90, 128, 255), flare);
			render_push_sprite(weapon->position, vec2i(size * 0.4, size * 0.4), rgba(110, 128, 128, 255), flare);
		}
	}

	explosions_draw();

	render_set_depth_offset(0.0);
	render_set_depth_write(true);
	render_set_blend_mode(RENDER_BLEND_NORMAL);
}



void weapon_set_trajectory(weapon_t *self) {
	ship_t *ship = self->owner;
	track_face_t *face = track_section_get_base_face(ship->section);

	vec3_t face_point = face->tris[0].vertices[0].pos;
	vec3_t target = vec3_transform(vec3(0,0,64), &ship->mat);
	
	float target_height = vec3_distance_to_plane(target, face_point, face->normal);
	float ship_height = vec3_distance_to_plane(target, face_point, face->normal);

	float nudge = target_height * 0.95 - ship_height;

	self->acceleration = vec3_sub(vec3_sub(target, vec3_mulf(face->normal, nudge)), ship->position);
	self->velocity = vec3_mulf(ship->velocity, 0.015625);
	self->angle = ship->angle;
}

void weapon_follow_target(weapon_t *self) {
	vec3_t angular_velocity = vec3(0, 0, 0);
	if (self->target) {
		vec3_t dir = vec3_mulf(vec3_sub(self->target->position, self->position), 0.125 * 30 * system_tick());
		float height = vec3_len(vec3_mul(dir, vec3(1,0,1)));
		angular_velocity.y = -atan2(dir.x, dir.z) - self->angle.y;
		angular_velocity.x = -atan2(dir.y, height) - self->angle.x;
	}

	angular_velocity = vec3_wrap_angle(angular_velocity);
	self->angle = vec3_add(self->angle, vec3_mulf(angular_velocity, 30 * system_tick() * 0.25));
	self->angle = vec3_wrap_angle(self->angle);

	mat4_t rotation_matrix;
	mat4_set_yaw_pitch_roll(&rotation_matrix, self->angle);
	self->acceleration = vec3_mulf(rotation_matrix.basis.forward.vec3, 256);
}

ship_t *weapon_collides_with_ship(weapon_t *self) {
	for (int i = 0; i < NUM_PILOTS; i++) {
		ship_t *ship = &g.ships[i];
		if (ship == self->owner) {
			continue;
		}

		float distance = vec3_len(vec3_sub(ship->position, self->position));
		if (distance < 512) {
			vec3_t base_vel = vec3_mulf(ship->velocity, 0.25);
			for (int p = 0; p < 32; p++) {
				vec3_t velocity = vec3_add(base_vel, vec3_rand(512));
				particles_spawn(self->position, self->ship_hit_particle, velocity, 256);
			}
			race_add_flash_light(self->position, self->ship_hit_particle);
			explosion_add(self->position, self->ship_hit_particle);
			return ship;
		}
	}

	return NULL;
}


bool weapon_collides_with_track(weapon_t *self) {
	if (flags_is(self->section->flags, SECTION_JUMP)) {
		return false;
	}

	track_face_t *face = g.track.faces + self->section->face_start;
	for (int i = 0; i < self->section->face_count; i++) {
		vec3_t face_point = face->tris[0].vertices[0].pos;
		float distance = vec3_distance_to_plane(self->position, face_point, face->normal);
		if (distance < 0) {
			return true;
		}
		face++;
	}

	return false;
}

void weapon_update_wait_for_delay(weapon_t *self) {
	if (self->timer <= 0) {
		weapons_fire(self->owner, self->type);
		self->active = false;
	}
}


void weapon_fire_mine(ship_t *ship) {
	float timer = 0;
	for (int i = 0; i < WEAPON_MINE_COUNT; i++) {
		weapon_t *self = weapon_init(ship);
		if (!self) {
			return;
		}
		timer += WEAPON_MINE_RELEASE_RATE;
		self->timer = timer;
		self->update_func = weapon_update_mine_wait_for_release;
	}
}



void weapon_update_mine_wait_for_release(weapon_t *self) {
	if (self->timer <= 0) {
		self->timer = WEAPON_MINE_DURATION;
		self->update_func = weapon_update_mine;
		self->model = weapon_assets.mine;
		self->position = self->owner->position;
		self->angle.y = rand_float(0, M_PI * 2);

		self->trail_particle = PARTICLE_TYPE_NONE;
		self->track_hit_particle = PARTICLE_TYPE_NONE;
		self->ship_hit_particle = PARTICLE_TYPE_FIRE;

		if (ship_is_player(self->owner)) {
			sfx_play(SFX_MINE_DROP);
		}
	}
}

void weapon_update_mine_lights(weapon_t *self, int index) {
	Prm prm = {.primitive = self->model->primitives};

	uint8_t r = sinf(system_cycle_time() * M_PI * 2 + index * 0.66) * 128 + 128;
	for (int i = 0; i < 8; i++) {
		switch (prm.primitive->type) {
		case PRM_TYPE_GT3:
			prm.gt3->color[0] = rgba(230, 0,    0, 0xFF);
			prm.gt3->color[1] = rgba(r,   0x40, 0, 0xFF);
			prm.gt3->color[2] = rgba(r,   0x40, 0, 0xFF);
			prm.gt3 += 1;
			break;
		}
	}
}

void weapon_update_mine(weapon_t *self) {
	if (self->timer <= 0) {
		self->active = false;
		return;
	}

	// TODO: oscillate perpendicular to track!?
	self->angle.y += system_tick();

	ship_t *ship = weapon_collides_with_ship(self);
	if (ship) {
		sfx_play_at(SFX_EXPLOSION_1, self->position, vec3(0,0,0), 1);
		self->active = false;
		if (flags_not(ship->flags, SHIP_SHIELDED)) {
			if (ship_is_player(ship)) {
				ship->velocity = vec3_sub(ship->velocity, vec3_mulf(ship->velocity, 0.125));
				camera_set_shake(game_ship_camera(ship), CAMERA_SHAKE_LONG);
			}
			else {
				ship->speed = ship->speed * 0.125;
			}
		}
	}	
}


void weapon_fire_missile(ship_t *ship) {
	weapon_t *self = weapon_init(ship);
	if (!self) {
		return;
	}

	self->timer = WEAPON_MISSILE_DURATION;
	self->model = weapon_assets.missile;
	self->update_func = weapon_update_missile;
	self->trail_particle = PARTICLE_TYPE_SMOKE;
	self->track_hit_particle = PARTICLE_TYPE_FIRE_WHITE;
	self->ship_hit_particle = PARTICLE_TYPE_FIRE;
	self->target = ship->weapon_target;
	self->drag = 0.25;
	weapon_set_trajectory(self);

	if (ship_is_player(self->owner)) {
		sfx_play(SFX_MISSILE_FIRE);
	}
}

void weapon_update_missile(weapon_t *self) {
	if (self->timer <= 0) {
		self->active = false;
		return;
	}

	weapon_follow_target(self);

	// Collision with other ships
	ship_t *ship = weapon_collides_with_ship(self);
	if (ship) {
		sfx_play_at(SFX_EXPLOSION_1, self->position, vec3(0,0,0), 1);
		self->active = false;

		if (flags_not(ship->flags, SHIP_SHIELDED)) {
			if (ship_is_player(ship)) {
				ship->velocity = vec3_sub(ship->velocity, vec3_mulf(ship->velocity, 0.75));
				ship->angular_velocity.z += rand_float(-0.1, 0.1);
				ship->turn_rate_from_hit = rand_float(-0.1, 0.1);
				camera_set_shake(game_ship_camera(ship), CAMERA_SHAKE_LONG);
			}
			else {
				ship->speed = ship->speed * 0.03125;
				ship->angular_velocity.z += 10 * M_PI;
				ship->turn_rate_from_hit = rand_float(-M_PI, M_PI);
			}
		}
	}
}

void weapon_fire_rocket(ship_t *ship) {
	weapon_t *self = weapon_init(ship);
	if (!self) {
		return;
	}

	self->timer = WEAPON_ROCKET_DURATION;
	self->model = weapon_assets.rocket;
	self->update_func = weapon_update_rocket;
	self->trail_particle = PARTICLE_TYPE_SMOKE;
	self->track_hit_particle = PARTICLE_TYPE_FIRE_WHITE;
	self->ship_hit_particle = PARTICLE_TYPE_FIRE;
	self->drag = 0.03125;
	weapon_set_trajectory(self);

	if (ship_is_player(self->owner)) {
		sfx_play(SFX_MISSILE_FIRE);
	}
}

void weapon_update_rocket(weapon_t *self) {
	if (self->timer <= 0) {
		self->active = false;
		return;
	}

	// Collision with other ships
	ship_t *ship = weapon_collides_with_ship(self);
	if (ship) {
		sfx_play_at(SFX_EXPLOSION_1, self->position, vec3(0,0,0), 1);
		self->active = false;

		if (flags_not(ship->flags, SHIP_SHIELDED)) {
			if (ship_is_player(ship)) {
				ship->velocity = vec3_mulf(ship->velocity, 0.25);
				ship->angular_velocity.z += rand_float(-0.1, 0.1);;
				ship->turn_rate_from_hit = rand_float(-0.1, 0.1);;
				camera_set_shake(game_ship_camera(ship), CAMERA_SHAKE_LONG);
			}
			else {
				ship->speed = ship->speed * 0.03125;
				ship->angular_velocity.z += 10 * M_PI;
				ship->turn_rate_from_hit = rand_float(-M_PI, M_PI);
			}
		}
	}
}


void weapon_fire_ebolt(ship_t *ship) {
	weapon_t *self = weapon_init(ship);
	if (!self) {
		return;
	}

	self->timer = WEAPON_EBOLT_DURATION;
	self->model = weapon_assets.ebolt;
	self->update_func = weapon_update_ebolt;
	self->trail_particle = PARTICLE_TYPE_EBOLT;
	self->track_hit_particle = PARTICLE_TYPE_EBOLT;
	self->ship_hit_particle = PARTICLE_TYPE_GREENY;
	self->target = ship->weapon_target;
	self->drag = 0.25;
	weapon_set_trajectory(self);

	if (ship_is_player(self->owner)) {
		sfx_play(SFX_EBOLT);
	}
}

void weapon_update_ebolt(weapon_t *self) {
	if (self->timer <= 0) {
		self->active = false;
		return;
	}

	weapon_follow_target(self);

	// Collision with other ships
	ship_t *ship = weapon_collides_with_ship(self);
	if (ship) {
		sfx_play_at(SFX_EXPLOSION_1, self->position, vec3(0,0,0), 1);
		self->active = false;

		if (flags_not(ship->flags, SHIP_SHIELDED)) {
			flags_add(ship->flags, SHIP_ELECTROED);
			ship->ebolt_timer = WEAPON_EBOLT_DURATION;
		}
	}
}

void weapon_fire_shield(ship_t *ship) {
	weapon_t *self = weapon_init(ship);
	if (!self) {
		return;
	}

	self->timer = WEAPON_SHIELD_DURATION;
	self->model = weapon_assets.shield;
	self->update_func = weapon_update_shield;

	flags_add(self->owner->flags, SHIP_SHIELDED);
}

void weapon_update_shield(weapon_t *self) {
	if (self->timer <= 0) {
		self->active = false;
		flags_rm(self->owner->flags, SHIP_SHIELDED);
		return;
	}


	if (flags_is(self->owner->flags, SHIP_VIEW_INTERNAL)) {
		self->position = ship_cockpit(self->owner);
		self->model = weapon_assets.shield_internal;
	}
	else {
		self->position = self->owner->position;
		self->model = weapon_assets.shield;
	}
	self->angle = self->owner->angle;

	// The colors are set in weapons_draw(), as they depend on the camera
}


void weapon_fire_turbo(ship_t *ship) {
	ship->velocity = vec3_add(ship->velocity, vec3_mulf(ship->mat.basis.forward.vec3, 39321)); // unitVecNose.vx) << 3) * FR60) / 50
	ship->turbo_timer = 1.4;

	// A blue-white flash from the engines
	vec3_t pos;
	float intensity;
	if (ship_exhaust_light(ship, &pos, &intensity)) {
		race_add_flash_light(pos, PARTICLE_TYPE_EBOLT);
	}
	
	if (ship_is_player(ship)) {
		sfx_t *sfx = sfx_play(SFX_MISSILE_FIRE);
		sfx->pitch = 0.25;
	}
}

int weapon_get_random_type(int type_class) {
	if (type_class == WEAPON_CLASS_ANY) {
		int index = rand_int(0, 65);
		if (index < 17) {
			return WEAPON_TYPE_ROCKET;
		}
		else if (index < 35) {
			return WEAPON_TYPE_MINE;
		}
		else if (index < 45) {
			return WEAPON_TYPE_SHIELD;
		}
		else if (index < 53) {
			return WEAPON_TYPE_MISSILE;
		}
		else if (index < 59) {
			return WEAPON_TYPE_TURBO;
		}
		else {
			return WEAPON_TYPE_EBOLT;
		}
	}
	else if (type_class == WEAPON_CLASS_PROJECTILE) { 
		int index = rand_int(0, 60);
		if (index < 27) {
			return WEAPON_TYPE_ROCKET;
		}
		else if (index < 40) {
			return WEAPON_TYPE_MISSILE;
		}
		else if (index < 50) {
			return WEAPON_TYPE_TURBO;
		}
		else {
			return WEAPON_TYPE_EBOLT;
		}
	}
	else {
		die("Unknown WEAPON_CLASS_ %d", type_class);
	}
}

