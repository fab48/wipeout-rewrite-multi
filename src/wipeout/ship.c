#include "../mem.h"
#include "../utils.h"
#include "../system.h"
#include "../render.h"

#include "object.h"
#include "track.h"
#include "weapon.h"
#include "image.h"
#include "ship.h"
#include "ship_ai.h"
#include "ship_player.h"
#include "game.h"
#include "race.h"
#include "sfx.h"
#include "particle.h"

#define EXHAUST_FLARE_TEXTURE_SIZE 128

static uint16_t exhaust_flare_texture;

// Procedural flare: a soft radial glow with a diagonal 4 point star. The
// intensity lives in the alpha channel, so it can be tinted through the vertex
// color and drawn with RENDER_BLEND_LIGHTER.
static uint16_t ship_create_exhaust_flare_texture(void) {
	int size = EXHAUST_FLARE_TEXTURE_SIZE;
	rgba_t *pixels = mem_temp_alloc(sizeof(rgba_t) * size * size);

	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			float u = ((x + 0.5) / size) * 2.0 - 1.0;
			float v = ((y + 0.5) / size) * 2.0 - 1.0;
			float falloff = max(0.0, 1.0 - sqrtf(u * u + v * v));

			float glow = falloff * falloff * falloff;
			float core = powf(falloff, 12);

			float diag = min(fabsf(u - v), fabsf(u + v)) * 0.7071 * 16.0;
			float star = falloff * falloff * expf(-diag * diag);

			float intensity = clamp(glow * 0.7 + core + star * 0.8, 0.0, 1.0);
			pixels[y * size + x] = rgba(128, 128, 128, intensity * 255);
		}
	}

	uint16_t texture = render_texture_create(size, size, pixels);
	mem_temp_free(pixels);
	return texture;
}

uint16_t ship_exhaust_flare_texture(void) {
	return exhaust_flare_texture;
}

void ships_load(void) {
	texture_list_t ship_textures = image_get_compressed_textures("wipeout/common/allsh.cmp");
	Object *ship_models = objects_load("wipeout/common/allsh.prm", ship_textures);

	texture_list_t collision_textures = image_get_compressed_textures("wipeout/common/alcol.cmp");
	Object *collision_models = objects_load("wipeout/common/alcol.prm", collision_textures);

	int object_index;
	Object *ship_model = ship_models;
	Object *collision_model = collision_models;

	for (object_index = 0; object_index < len(g.ships) && ship_model && collision_model; object_index++) {
		int ship_index = def.ship_model_to_pilot[object_index];
		g.ships[ship_index].model = ship_model;
		g.ships[ship_index].collision_model = collision_model;

		ship_model = ship_model->next;
		collision_model = collision_model->next;

		ship_init_exhaust_plume(&g.ships[ship_index]);
	}

	error_if(object_index != len(g.ships), "Expected %ld ship models, got %d", len(g.ships), object_index);

	uint16_t shadow_textures_start = render_textures_len();
	image_get_texture_semi_trans("wipeout/textures/shad1.tim");
	image_get_texture_semi_trans("wipeout/textures/shad2.tim");
	image_get_texture_semi_trans("wipeout/textures/shad3.tim");
	image_get_texture_semi_trans("wipeout/textures/shad4.tim");

	for (int i = 0; i < len(g.ships); i++) {
		g.ships[i].shadow_texture = shadow_textures_start + (i >> 1);
		g.ships[i].exhaust_trail_valid = false;
	}

	exhaust_flare_texture = ship_create_exhaust_flare_texture();
}


void ships_init(section_t *section) {
	section_t *start_sections[len(g.ships)];

	int ranks_to_pilots[NUM_PILOTS];

	// Initialize ranks with all pilots in order
	for (int i = 0; i < len(g.ships); i++) {
		ranks_to_pilots[i] = i;
	}

	// Randomize order for single race or new championship
	if (g.race_type != RACE_TYPE_CHAMPIONSHIP || g.circuit == CIRCUIT_ALTIMA_VII) {
		shuffle(ranks_to_pilots, len(ranks_to_pilots));
	}

	// Randomize some tiers in an ongoing championship
	else if (g.race_type == RACE_TYPE_CHAMPIONSHIP) {
		// Initialize with current championship order
		for (int i = 0; i < len(g.ships); i++) {
			ranks_to_pilots[i] = g.championship_ranks[i].pilot;
		}		
		shuffle(ranks_to_pilots, 2); // shuffle 0..1
		shuffle(ranks_to_pilots + 4, len(ranks_to_pilots)-5); // shuffle 4..len-1
	}

	// player is always last
	for (int i = 0; i < len(ranks_to_pilots)-1; i++) {
		if (ranks_to_pilots[i] == g.pilot) {
			swap(ranks_to_pilots[i], ranks_to_pilots[i+1]);
		}
	}

	// player 2 starts right in front of player 1
	if (g.num_players > 1) {
		for (int i = 0; i < len(ranks_to_pilots)-2; i++) {
			if (ranks_to_pilots[i] == g.pilot2) {
				swap(ranks_to_pilots[i], ranks_to_pilots[i+1]);
			}
		}
	}


	int start_line_pos = def.circuits[g.circuit].settings[g.race_class].start_line_pos;
	for (int i = 0; i < start_line_pos - 15; i++) {
		section = section->next;
	}
	for (int i = 0; i < len(g.ships); i++) {
		start_sections[i] = section;
		section = section->next;
		if ((i % 2) == 0) {
			section = section->next;
		}
	}

	for (int i = 0; i < len(ranks_to_pilots); i++) {
		int rank_inv = (len(g.ships)-1) - i;
		int pilot = ranks_to_pilots[i];
		ship_init(&g.ships[pilot], start_sections[rank_inv], pilot, rank_inv);
	}
}

static inline bool sort_rank_compare(pilot_points_t *pa, pilot_points_t *pb) {
	ship_t *a = &g.ships[pa->pilot];
	ship_t *b = &g.ships[pb->pilot];
	if (a->total_section_num == b->total_section_num) {
		vec3_t c0 = a->section->center;
		vec3_t c1 = a->section->next->center;
		vec3_t dir = vec3_sub(c1, c0);
		float pos_a = vec3_dot(vec3_sub(a->position, c0), dir);
		float pos_b = vec3_dot(vec3_sub(b->position, c0), dir);
		return (pos_a < pos_b);
	}
	else {
		return a->total_section_num < b->total_section_num;
	}
}

void ships_update(void) {
	if (g.race_type == RACE_TYPE_TIME_TRIAL) {
		ship_update(&g.ships[g.pilot]);
	}
	else {
		for (int i = 0; i < len(g.ships); i++) {
			ship_update(&g.ships[i]);
		}
		for (int j = 0; j < (len(g.ships) - 1); j++) {
			for (int i = j + 1; i < len(g.ships); i++) {
				ship_collide_with_ship(&g.ships[i], &g.ships[j]);
			}
		}

		bool is_racing = false;
		for (int p = 0; p < g.num_players; p++) {
			if (flags_is(game_player_ship(p)->flags, SHIP_RACING)) {
				is_racing = true;
			}
		}
		if (is_racing) {
			sort(g.race_ranks, len(g.race_ranks), sort_rank_compare);
			for (int32_t i = 0; i < len(g.ships); i++) {
				g.ships[g.race_ranks[i].pilot].position_rank = i + 1;
			}
		}
	}
}

void ships_reset_exhaust_plumes(void) {
	for (int i = 0; i < len(g.ships); i++) {
		ship_reset_exhaust_plume(&g.ships[i]);
	}
}


static bool ship_exhaust_is_hidden(int i) {
	return (
		(
			g.ships[i].player == g.view_player &&
			flags_is(g.ships[i].flags, SHIP_VIEW_INTERNAL) &&
			flags_not(g.ships[i].flags, SHIP_IN_RESCUE)
		) ||
		(g.race_type == RACE_TYPE_TIME_TRIAL && i != g.pilot)
	);
}

void ships_draw(void) {
	render_set_material(RENDER_MATERIAL_SHIP);

	// Ship models
	for (int i = 0; i < len(g.ships); i++) {
		if (ship_exhaust_is_hidden(i)) {
			continue;
		}

		ship_draw(&g.ships[i]);
	}


	// Shadows
	render_set_material(RENDER_MATERIAL_UNLIT);
	render_set_model_mat(&mat4_identity());

	render_set_depth_write(false);
	render_set_depth_offset(-32.0);

	for (int i = 0; i < len(g.ships); i++) {
		if (
			(g.race_type == RACE_TYPE_TIME_TRIAL && i != g.pilot) ||
			flags_not(g.ships[i].flags, SHIP_VISIBLE) || 
			flags_is(g.ships[i].flags, SHIP_FLYING)
		) {
			continue;
		}

		ship_draw_shadow(&g.ships[i]);
	}

	// Exhaust plumes, trails and flares; all additive, drawn after everything
	// opaque so they never punch holes into the depth buffer
	render_set_blend_mode(RENDER_BLEND_LIGHTER);

	render_set_depth_offset(0.0);
	for (int i = 0; i < len(g.ships); i++) {
		if (ship_exhaust_is_hidden(i)) {
			continue;
		}
		ship_draw_exhaust_plume(&g.ships[i]);
	}

	render_set_model_mat(&mat4_identity());
	render_set_depth_offset(-32.0);
	render_set_cull_backface(false);
	for (int i = 0; i < len(g.ships); i++) {
		if (ship_exhaust_is_hidden(i)) {
			continue;
		}
		ship_draw_exhaust_glow(&g.ships[i]);
	}
	render_set_cull_backface(true);

	render_set_blend_mode(RENDER_BLEND_NORMAL);
	render_set_depth_offset(0.0);
	render_set_depth_write(true);
	render_set_material(RENDER_MATERIAL_SHIP);
}







void ship_init(ship_t *self, section_t *section, int pilot, int inv_start_rank) {
	self->pilot = pilot;
	self->velocity = vec3(0, 0, 0);
	self->acceleration = vec3(0, 0, 0);
	self->angle = vec3(0, 0, 0);
	self->angular_velocity = vec3(0, 0, 0);
	self->turn_rate = 0;
	self->thrust_mag = 0;
	self->current_thrust_max = 0;
	self->turn_rate_from_hit = 0;
	self->brake_right = 0;
	self->brake_left = 0;
	self->flags = SHIP_RACING | SHIP_VISIBLE | SHIP_DIRECTION_FORWARD;
	self->weapon_type = WEAPON_TYPE_NONE;
	self->lap = -1;
	self->max_lap = -1;
	self->speed = 0;
	self->ebolt_timer = 0;
	self->revcon_timer = 0;
	self->special_timer = 0;
	self->weapon_target = NULL;
	self->mat = mat4_identity();

	self->update_timer = 0;
	self->last_impact_time = 0;

	int team = def.pilots[pilot].team;
	self->mass =          def.teams[team].attributes[g.race_class].mass;
	self->thrust_max =    def.teams[team].attributes[g.race_class].thrust_max;
	self->skid =          def.teams[team].attributes[g.race_class].skid;
	self->turn_rate =     def.teams[team].attributes[g.race_class].turn_rate;
	self->turn_rate_max = def.teams[team].attributes[g.race_class].turn_rate_max;
	self->resistance =    def.teams[team].attributes[g.race_class].resistance;
	self->lap_time = 0;

	self->update_timer = UPDATE_TIME_INITIAL;
	self->position_rank = NUM_PILOTS - inv_start_rank;

	self->player = -1;
	if (pilot == g.pilot) {
		self->player = 0;
	}
	else if (g.num_players > 1 && pilot == g.pilot2) {
		self->player = 1;
	}

	if (ship_is_player(self)) {
		self->update_func = ship_player_update_intro;
		self->remote_thrust_max = 2900;
		self->remote_thrust_mag = 46;
		self->fight_back = 0;
	}
	else {
		self->update_func = ship_ai_update_intro;
		self->remote_thrust_max = def.ai_settings[g.race_class][inv_start_rank-1].thrust_max;
		self->remote_thrust_mag = def.ai_settings[g.race_class][inv_start_rank-1].thrust_magnitude;
		self->fight_back = def.ai_settings[g.race_class][inv_start_rank-1].fight_back;
	}

	self->section = section;
	self->prev_section = section;
	float spread_base = def.circuits[g.circuit].settings[g.race_class].spread_base;
	float spread_factor = def.circuits[g.circuit].settings[g.race_class].spread_factor;
	int p = inv_start_rank - 1;
	self->start_accelerate_timer = p * (spread_base + (p * spread_factor)) * (1.0/30.0);

	track_face_t *face = g.track.faces + section->face_start;
	face++;
	if ((inv_start_rank % 2) != 0) {
		face++;
	}
	
	vec3_t face_point = vec3_mulf(vec3_add(face->tris[0].vertices[0].pos, face->tris[0].vertices[2].pos), 0.5);
	self->position = vec3_add(face_point, vec3_mulf(face->normal, 200));

	self->section_num = section->num;
	self->prev_section_num = section->num;
	self->total_section_num = section->num;

	section_t *next = section->next;
	vec3_t direction = vec3_sub(next->center, section->center);
	self->angle.y = -atan2(direction.x, direction.z);
}

const rgba_t exhaust_plume_color = rgba(96,150,255,230);
const rgba_t exhaust_plume_tip_color = rgba(16,40,255,40);
const rgba_t exhaust_trail_color = rgba(32,72,255,255);
const rgba_t exhaust_flare_color = rgba(48,96,255,255);
const rgba_t exhaust_flare_core_color = rgba(128,144,200,255);

void ship_init_exhaust_plume(ship_t *self) {
	int16_t indices[64];
	int16_t indices_len = 0;

	Prm prm = {.primitive = self->model->primitives};

	for (int i = 0; i < self->model->primitives_len; i++) {
		if (flags_is(prm.primitive->flag, PRM_SHIP_ENGINE)) {
			flags_add(prm.primitive->flag, PRM_TRANSLUCENT);

			switch (prm.primitive->type) {
			case PRM_TYPE_FT3:
				indices[indices_len++] = prm.ft3->coords[0];
				indices[indices_len++] = prm.ft3->coords[1];
				indices[indices_len++] = prm.ft3->coords[2];

				prm.ft3->color = exhaust_plume_color;
				prm.ft3->texture = RENDER_NO_TEXTURE;
				prm.ft3->u0 = prm.ft3->v0 = 0;
				prm.ft3->u1 = prm.ft3->v1 = 0;
				prm.ft3->u2 = prm.ft3->v2 = 0;
				prm.ft3++;
				break;
			case PRM_TYPE_GT3:
				indices[indices_len++] = prm.gt3->coords[0];
				indices[indices_len++] = prm.gt3->coords[1];
				indices[indices_len++] = prm.gt3->coords[2];

				for (int j = 0; j < 3; j++) {
					prm.gt3->color[j] = exhaust_plume_color;
				}
				prm.gt3->texture = RENDER_NO_TEXTURE;
				prm.gt3->u0 = prm.gt3->v0 = 0;
				prm.gt3->u1 = prm.gt3->v1 = 0;
				prm.gt3->u2 = prm.gt3->v2 = 0;
				prm.gt3++;
				break;
			default:
				die("Primitive type %x is marked as an engine primitive but is not ft3 or gt3\n", prm.primitive->type);
			}
		} else {
			switch (prm.primitive->type) {
			case PRM_TYPE_F3: prm.f3++; break;
			case PRM_TYPE_F4: prm.f4++; break;
			case PRM_TYPE_FT3: prm.ft3++; break;
			case PRM_TYPE_FT4: prm.ft4++; break;
			case PRM_TYPE_G3: prm.g3++; break;
			case PRM_TYPE_G4: prm.g4++; break;
			case PRM_TYPE_GT3: prm.gt3++; break;
			case PRM_TYPE_GT4: prm.gt4++; break;
			default:
				die("Bad primitive type %x\n", prm.primitive->type);
			}
		}
	}


	// get out the center vertex

	self->exhaust_plume[0].v = NULL;
	self->exhaust_plume[1].v = NULL;
	self->exhaust_plume[2].v = NULL;

	int shared[3] = {-1, -1, -1};
	int booster = 0;
	for (int i = 0; (i < indices_len) && (booster < 3); i++) {
		int similar = 0;
		for (int j = 0; j < indices_len; j++) {
			if (indices[i] == indices[j]) {
				similar++;
				if (similar > 3) {
					bool found = false;
					for (int k = 0; k < 3; k++) {
						if (shared[k] == indices[i]) {
							found = true;
						}
					}

					if (!found) {
						shared[booster++] = indices[i];
					}
				}
			}
		}
	}

	for (int j = 0; j < 3; j++) {
		if (shared[j] != -1) {
			self->exhaust_plume[j].v = &self->model->vertices[shared[j]];
			self->exhaust_plume[j].initial = self->model->vertices[shared[j]];
		}
	}

	// Fade the gouraud shaded plumes out towards their tip and find the center
	// of each plume's base (the nozzle), which is where the flare sits
	vec3_t base_sum[3] = {vec3(0, 0, 0), vec3(0, 0, 0), vec3(0, 0, 0)};
	int base_count[3] = {0, 0, 0};

	prm.primitive = self->model->primitives;
	for (int i = 0; i < self->model->primitives_len; i++) {
		switch (prm.primitive->type) {
		case PRM_TYPE_F3: prm.f3++; break;
		case PRM_TYPE_F4: prm.f4++; break;
		case PRM_TYPE_FT4: prm.ft4++; break;
		case PRM_TYPE_G3: prm.g3++; break;
		case PRM_TYPE_G4: prm.g4++; break;
		case PRM_TYPE_GT4: prm.gt4++; break;
		case PRM_TYPE_FT3:
		case PRM_TYPE_GT3:
			if (flags_is(prm.primitive->flag, PRM_SHIP_ENGINE)) {
				// coords[] is at the same offset for FT3 and GT3
				int16_t *coords = prm.ft3->coords;
				for (int k = 0; k < 3; k++) {
					if (shared[k] == -1 || (coords[0] != shared[k] && coords[1] != shared[k] && coords[2] != shared[k])) {
						continue;
					}
					for (int j = 0; j < 3; j++) {
						if (coords[j] == shared[k]) {
							if (prm.primitive->type == PRM_TYPE_GT3) {
								prm.gt3->color[j] = exhaust_plume_tip_color;
							}
						}
						else {
							base_sum[k] = vec3_add(base_sum[k], self->model->vertices[coords[j]]);
							base_count[k]++;
						}
					}
				}
			}
			if (prm.primitive->type == PRM_TYPE_GT3) {
				prm.gt3++;
			}
			else {
				prm.ft3++;
			}
			break;
		default:
			break;
		}
	}

	for (int k = 0; k < 3; k++) {
		if (shared[k] != -1) {
			self->exhaust_plume[k].base = base_count[k] > 0
				? vec3_mulf(base_sum[k], 1.0 / base_count[k])
				: self->exhaust_plume[k].initial;
		}
	}
}

void ship_reset_exhaust_plume(ship_t* self) {
	for (int i = 0; i < 3; i++) {
		if (self->exhaust_plume[i].v)
			*self->exhaust_plume[i].v = self->exhaust_plume[i].initial;
	}
}


void ship_draw(ship_t *self) {
	// The engine primitives are drawn separately in ship_draw_exhaust_plume()
	object_draw_filtered(self->model, &self->mat, PRM_SHIP_ENGINE, false);
}

void ship_draw_exhaust_plume(ship_t *self) {
	object_draw_filtered(self->model, &self->mat, PRM_SHIP_ENGINE, true);
}

static void ship_draw_exhaust_trail(ship_t *self, vec3_t *trail) {
	float intensity = 0.25 + 0.75 * min(self->exhaust_intensity, 1.0);
	float turbo = max(self->exhaust_intensity - 1.0, 0.0); // 0..0.6 during the turbo
	vec3_t side = vec3(0, 0, 0);

	vec3_t prev_pos[3];
	rgba_t prev_color[3];
	bool has_prev = false;

	for (int i = 0; i < SHIP_EXHAUST_TRAIL_POINTS; i++) {
		// Ribbon side vector at this point, facing the camera. Keep the last
		// known one if the points are too close together.
		int a = i == 0 ? 0 : i - 1;
		int b = i == SHIP_EXHAUST_TRAIL_POINTS - 1 ? i : i + 1;
		vec3_t dir = vec3_sub(trail[a], trail[b]);
		vec3_t new_side = vec3_cross(dir, vec3_sub(g.camera->position, trail[i]));
		float new_side_len = vec3_len(new_side);
		if (new_side_len > 0.001 && vec3_len(dir) > 1.0) {
			side = vec3_mulf(new_side, 1.0 / new_side_len);
		}
		else if (!has_prev) {
			continue;
		}

		float t = (float)i / (float)(SHIP_EXHAUST_TRAIL_POINTS - 1);
		// Fade in over the first few points, so the trail doesn't start as a 
		// hard edge right at the nozzle
		float fade = (1.0 - t) * (1.0 - t) * min(1.0, i / 3.0);
		float width = (14.0 + 34.0 * (1.0 - t)) * (1.0 + turbo * 1.2);

		vec3_t pos[3] = {
			vec3_sub(trail[i], vec3_mulf(side, width)),
			trail[i],
			vec3_add(trail[i], vec3_mulf(side, width))
		};
		rgba_t trail_color = exhaust_trail_color;
		trail_color.r = min(255, trail_color.r + (int)(turbo * 200));
		trail_color.g = min(255, trail_color.g + (int)(turbo * 160));
		rgba_t color[3] = {trail_color, trail_color, trail_color};
		color[0].a = 0;
		color[1].a = 255 * fade * intensity;
		color[2].a = 0;

		if (has_prev) {
			for (int j = 0; j < 2; j++) {
				render_push_tris((tris_t) {
					.vertices = {
						{.pos = prev_pos[j],   .color = prev_color[j]},
						{.pos = prev_pos[j+1], .color = prev_color[j+1]},
						{.pos = pos[j],        .color = color[j]},
					}
				}, RENDER_NO_TEXTURE);
				render_push_tris((tris_t) {
					.vertices = {
						{.pos = pos[j],        .color = color[j]},
						{.pos = prev_pos[j+1], .color = prev_color[j+1]},
						{.pos = pos[j+1],      .color = color[j+1]},
					}
				}, RENDER_NO_TEXTURE);
			}
		}

		for (int j = 0; j < 3; j++) {
			prev_pos[j] = pos[j];
			prev_color[j] = color[j];
		}
		has_prev = true;
	}
}

// Draws the additive trail and flare for each engine. Expects an identity
// model mat, RENDER_BLEND_LIGHTER and depth writes to be disabled.
void ship_draw_exhaust_glow(ship_t *self) {
	if (!self->exhaust_trail_valid) {
		return;
	}

	int engines = 0;
	for (int i = 0; i < 3; i++) {
		if (self->exhaust_plume[i].v) {
			engines++;
		}
	}
	if (engines == 0) {
		return;
	}

	// The flare follows the exhaust plume, both in size and position
	float base_size = clamp(110.0 + self->exhaust_len * 0.8, 110.0, 260.0);
	if (engines > 1) {
		base_size *= 0.8;
	}

	for (int i = 0; i < 3; i++) {
		if (!self->exhaust_plume[i].v) {
			continue;
		}
		vec3_t *trail = self->exhaust_plume[i].trail;
		ship_draw_exhaust_trail(self, trail);

		// The flare sits inside the plume, close to the nozzle. Some ships have
		// their nozzle recessed into the hull, so pull the flare a bit towards
		// the camera to not have it cut off by the surrounding hull polygons.
		vec3_t nozzle = vec3_lerp(self->exhaust_plume[i].base, self->exhaust_plume[i].initial, 0.35);
		vec3_t flare_pos = vec3_transform(nozzle, &self->mat);
		vec3_t to_camera = vec3_sub(g.camera->position, flare_pos);
		float to_camera_len = vec3_len(to_camera);
		if (to_camera_len > 1.0) {
			float pull = min(64.0, to_camera_len * 0.5);
			flare_pos = vec3_add(flare_pos, vec3_mulf(to_camera, pull / to_camera_len));
		}

		int size = base_size * rand_float(0.85, 1.15);
		int core_size = size * 0.4;
		render_push_sprite(flare_pos, vec2i(size, size), exhaust_flare_color, exhaust_flare_texture);
		render_push_sprite(flare_pos, vec2i(core_size, core_size), exhaust_flare_core_color, exhaust_flare_texture);
	}
}

// Position and brightness of the light emitted by the exhaust flares
bool ship_exhaust_light(ship_t *self, vec3_t *pos, float *intensity) {
	if (!self->exhaust_trail_valid) {
		return false;
	}
	vec3_t sum = vec3(0, 0, 0);
	int engines = 0;
	for (int i = 0; i < 3; i++) {
		if (self->exhaust_plume[i].v) {
			sum = vec3_add(sum, self->exhaust_plume[i].trail[0]);
			engines++;
		}
	}
	if (engines == 0) {
		return false;
	}
	*pos = vec3_mulf(sum, 1.0 / engines);
	*intensity = 0.5 + 0.5 * self->exhaust_intensity;
	return true;
}

static void ship_update_exhaust_trail(ship_t *self) {
	float target = 1.0;
	if (ship_is_player(self) && self->thrust_max > 0) {
		target = clamp(self->thrust_mag / self->thrust_max, 0.0, 1.0);
	}
	if (self->turbo_timer > 0) {
		self->turbo_timer -= system_tick();
		target = 1.6;
	}
	self->exhaust_intensity += (target - self->exhaust_intensity) * min(1.0, system_tick() * 8.0);

	self->exhaust_trail_timer -= system_tick();
	bool shift = self->exhaust_trail_timer <= 0;
	if (shift) {
		self->exhaust_trail_timer = SHIP_EXHAUST_TRAIL_INTERVAL;
	}

	for (int i = 0; i < 3; i++) {
		if (!self->exhaust_plume[i].v) {
			continue;
		}

		vec3_t *trail = self->exhaust_plume[i].trail;
		vec3_t head = vec3_transform(self->exhaust_plume[i].initial, &self->mat);

		// Start over if we have no trail yet or the ship was teleported
		if (!self->exhaust_trail_valid || vec3_len(vec3_sub(head, trail[0])) > 4096) {
			for (int j = 0; j < SHIP_EXHAUST_TRAIL_POINTS; j++) {
				trail[j] = head;
			}
		}
		else if (shift) {
			for (int j = SHIP_EXHAUST_TRAIL_POINTS - 1; j > 0; j--) {
				trail[j] = trail[j - 1];
			}
		}
		trail[0] = head;
	}
	self->exhaust_trail_valid = true;
}

void ship_draw_shadow(ship_t *self) {	
	track_face_t *face = track_section_get_base_face(self->section);

	vec3_t face_point = face->tris[0].vertices[0].pos;
	vec3_t nose = vec3_transform(vec3( 0,   0,  384), &self->mat);
	vec3_t wngl = vec3_transform(vec3(-256, 0, -384), &self->mat);
	vec3_t wngr = vec3_transform(vec3( 256, 0, -384), &self->mat);

	// The higher the ship flies, the larger and fainter its shadow
	float height = max(0.0, vec3_distance_to_plane(self->position, face_point, face->normal) - 150.0);
	float fade = 1.0 - clamp(height / 2200.0, 0.0, 0.85);
	float grow = 1.0 + clamp(height / 2200.0, 0.0, 1.0) * 0.6;

	nose = vec3_sub(nose, vec3_mulf(face->normal, vec3_distance_to_plane(nose, face_point, face->normal)));
	wngl = vec3_sub(wngl, vec3_mulf(face->normal, vec3_distance_to_plane(wngl, face_point, face->normal)));
	wngr = vec3_sub(wngr, vec3_mulf(face->normal, vec3_distance_to_plane(wngr, face_point, face->normal)));

	vec3_t center = vec3_mulf(vec3_add(vec3_add(nose, wngl), wngr), 1.0 / 3.0);

	// A single layer, scaled and faded with the flight height
	{
		rgba_t color = rgba(0, 0, 0, 128 * fade);
		vec3_t n = vec3_add(center, vec3_mulf(vec3_sub(nose, center), grow));
		vec3_t l = vec3_add(center, vec3_mulf(vec3_sub(wngl, center), grow));
		vec3_t r = vec3_add(center, vec3_mulf(vec3_sub(wngr, center), grow));
		render_push_tris((tris_t) {
			.vertices = {
				{.pos = l, .uv = {0, 256},   .color = color},
				{.pos = r, .uv = {128, 256}, .color = color},
				{.pos = n, .uv = {64, 0},    .color = color},
			}
		}, self->shadow_texture);
	}
}

void ship_update(ship_t *self) {
	self->prev_section = self->section;

	// To find the nearest section to the ship, the original source de-emphasizes
	// the .y component when calculating the distance to each section by a 
	// >> 2 shift. I.e. it tries to find the section that is more closely to the
	// horizontal x,z plane (directly underneath the ship), instead of finding 
	// the section with the "real" closest distance. Hence the bias of 
	// vec3(1, 0.25, 1) here.
	float distance;
	self->section = track_nearest_section(self->position, vec3(1, 0.25, 1), self->section, &distance);
	if (distance > 3700) {
		flags_add(self->flags, SHIP_FLYING);
	}
	else {
		flags_rm(self->flags, SHIP_FLYING);
	}

	self->prev_section_num = self->prev_section->num;
	self->section_num = self->section->num;


	// Figure out which side of the track the ship is on
	track_face_t *face = track_section_get_base_face(self->section);

	vec3_t to_face_vector = vec3_sub(
		face->tris[0].vertices[0].pos,
		face->tris[0].vertices[1].pos
	);

	vec3_t direction = vec3_sub(self->section->center, self->position);

	if (vec3_dot(direction, to_face_vector) > 0) {
		flags_add(self->flags, SHIP_LEFT_SIDE);
	}
	else {
		flags_rm(self->flags, SHIP_LEFT_SIDE);
		face++;
	}

	// Collect powerup
	if (
		flags_is(face->flags, FACE_PICKUP_ACTIVE) &&
		flags_not(self->flags, SHIP_SPECIALED) &&
		self->weapon_type == WEAPON_TYPE_NONE &&
		track_collect_pickups(face)
	) {
		if (ship_is_player(self)) {
			sfx_play(SFX_POWERUP);
			if (flags_is(self->flags, SHIP_SHIELDED)) {
				self->weapon_type = weapon_get_random_type(WEAPON_CLASS_PROJECTILE);
			}
			else {
				self->weapon_type = weapon_get_random_type(WEAPON_CLASS_ANY);
			}
		}
		else {
			self->weapon_type = 1;
		}
	}

	self->last_impact_time += system_tick();
	
	// Call the active player/ai update function
	(self->update_func)(self);


	// Animate the exhaust plume

	int exhaust_len;

	if (ship_is_player(self)) {
		// get the z exhaust_len related to speed or thrust
		exhaust_len = self->thrust_mag * 0.0625;
		exhaust_len += self->speed * 0.00390625;
	}
	else {
		// for remote ships the z exhaust_len is a constant
		exhaust_len = 150;
	}

	if (self->turbo_timer > 0) {
		exhaust_len *= 1.8;
	}

	for (int i = 0; i < 3; i++) {
		self->exhaust_len = exhaust_len;
		if (self->exhaust_plume[i].v) {
			vec3_t jitter = vec3_rand(7);
			jitter.z *= 4;
			*self->exhaust_plume[i].v = vec3_add(self->exhaust_plume[i].initial, jitter);
			self->exhaust_plume[i].v->z -= exhaust_len;
		}
	}

	mat4_set_translation(&self->mat, self->position);
	mat4_set_yaw_pitch_roll(&self->mat, self->angle);

	ship_update_exhaust_trail(self);



	// Race position and lap times
	
	self->lap_time += system_tick();

	int start_line_pos = def.circuits[g.circuit].settings[g.race_class].start_line_pos;

	// Crossed line backwards
	if (self->prev_section_num == start_line_pos + 1 && self->section_num <= start_line_pos) {
		self->lap--;
	}

	// Crossed line forwards
	else if (self->prev_section_num == start_line_pos && self->section_num > start_line_pos) {
		self->lap++;

		// Is it the first time we're crossing the line for this lap?
		if (self->lap > self->max_lap) {
			self->max_lap = self->lap;

			if (self->lap > 0 && self->lap <= NUM_LAPS) {
				g.lap_times[self->pilot][self->lap-1] = self->lap_time;
			}
			self->lap_time = 0;

			if (g.race_type == RACE_TYPE_TIME_TRIAL) {
				self->weapon_type = WEAPON_TYPE_TURBO;
			}

			if (self->lap == NUM_LAPS && ship_is_player(self)) {
				race_player_finished(self);
			}
		}
	}

	int section_num_from_line = self->section_num - (start_line_pos + 1);
	if (section_num_from_line < 0) {
		section_num_from_line += g.track.section_count;
	}
	self->total_section_num = self->lap * g.track.section_count + section_num_from_line;
}

vec3_t ship_cockpit(ship_t *self) {
	return vec3_transform(vec3(0, -128, 0), &self->mat);
}

vec3_t ship_nose(ship_t *self) {
	return vec3_transform(vec3(0, 0, 512), &self->mat);
}

vec3_t ship_wing_left(ship_t *self) {
	return vec3_transform(vec3(-256, 0, -256), &self->mat);
}

vec3_t ship_wing_right(ship_t *self) {
	return vec3_transform(vec3(256, 0, -256), &self->mat);
}

static bool vec3_is_on_face(vec3_t pos, track_face_t *face, float alpha) {
	vec3_t plane_point = vec3_sub(pos, vec3_mulf(face->normal, alpha));
	vec3_t vec0 = vec3_sub(plane_point, face->tris[0].vertices[1].pos);
	vec3_t vec1 = vec3_sub(plane_point, face->tris[0].vertices[2].pos);
	vec3_t vec2 = vec3_sub(plane_point, face->tris[0].vertices[0].pos);
	vec3_t vec3 = vec3_sub(plane_point, face->tris[1].vertices[0].pos);

	float angle = 
		vec3_angle(vec0, vec2) +
		vec3_angle(vec2, vec3) +
		vec3_angle(vec3, vec1) +
		vec3_angle(vec1, vec0);

	return (angle > (0.91552734375 * M_PI * 2));
}

// Sparks flying off an impact point. strength 0..1 scales the amount and the
// spread; a hard hit adds fire and a flash of light.
void ship_spawn_impact_sparks(ship_t *self, vec3_t pos, vec3_t normal, float strength) {
	strength = clamp(strength, 0.0, 1.0);
	int count = 4 + (int)(strength * 22);
	vec3_t base = vec3_mulf(normal, 400 + 900 * strength);
	// Drag some of the ship's motion along, so the sparks streak the right way
	vec3_t drift = vec3_mulf(self->velocity, 0.01);

	for (int i = 0; i < count; i++) {
		vec3_t velocity = vec3_add(vec3_add(base, drift), vec3_rand(500 + 700 * strength));
		int size = 24 + rand_int(0, 40 + (int)(strength * 40));
		particles_spawn(pos, PARTICLE_TYPE_FIRE_WHITE, velocity, size);
	}

	if (strength > 0.55) {
		int fire = 3 + (int)((strength - 0.55) * 20);
		for (int i = 0; i < fire; i++) {
			vec3_t velocity = vec3_add(vec3_mulf(base, 0.5), vec3_rand(400));
			particles_spawn(pos, PARTICLE_TYPE_FIRE, velocity, 90 + rand_int(0, 90));
		}
		race_add_flash_light(pos, PARTICLE_TYPE_FIRE_WHITE);
	}
}

void ship_resolve_wing_collision(ship_t *self, track_face_t *face, float direction) {
	vec3_t collision_vector = vec3_sub(self->section->center, face->tris[0].vertices[2].pos);
	float angle = vec3_angle(collision_vector, self->mat.basis.forward.vec3);
	self->velocity = vec3_reflect(self->velocity, face->normal, 2);
	self->position = vec3_sub(self->position, vec3_mulf(self->velocity, 0.015625)); // system_tick?
	self->velocity = vec3_sub(self->velocity, vec3_mulf(self->velocity, 0.5));
	self->velocity = vec3_add(self->velocity, vec3_mulf(face->normal, 4096.0)); // div by 4096?

	float magnitude = (fabsf(angle) * self->speed) * 2 * M_PI / 4096.0; // (6 velocity shift, 12 angle shift?)

	vec3_t wing_pos;
	if (direction > 0) {
		self->angular_velocity.z += magnitude;
		wing_pos = ship_wing_right(self);
	}
	else {
		self->angular_velocity.z -= magnitude;	
		wing_pos = ship_wing_left(self);
	}

	if (self->last_impact_time > 0.2) {
		self->last_impact_time = 0;
		sfx_play_at(SFX_IMPACT, wing_pos, vec3(0, 0, 0), 1);
		// Grazing the wall: a few sparks; a hard, angled hit: a shower
		ship_spawn_impact_sparks(self, wing_pos, face->normal, 0.15 + fabsf(angle) * 0.5 + self->speed / 40000.0);
	}
}


void ship_resolve_nose_collision(ship_t *self, track_face_t *face, float direction) {
	vec3_t collision_vector = vec3_sub(self->section->center, face->tris[0].vertices[2].pos);
	// TODO: In the PSX original, nose collisions change depending on the angle to the wall,
	// but here this variable goes unused.
	float angle = vec3_angle(collision_vector, self->mat.basis.forward.vec3);
	self->velocity = vec3_reflect(self->velocity, face->normal, 2);
	self->position = vec3_sub(self->position, vec3_mulf(self->velocity, 0.015625)); // system_tick?
	self->velocity = vec3_sub(self->velocity, vec3_mulf(self->velocity, 0.5));
	self->velocity = vec3_add(self->velocity, vec3_mulf(face->normal, 4096)); // div by 4096?

	float magnitude = ((self->speed * 0.0625) + 400) * 2 * M_PI / 4096.0;
	if (direction > 0) {
		self->angular_velocity.y += magnitude;
	}
	else { 
		self->angular_velocity.y -= magnitude;
	}

	if (self->last_impact_time > 0.2) {
		self->last_impact_time = 0;
		sfx_play_at(SFX_IMPACT, ship_nose(self), vec3(0, 0, 0), 1);
		// Head on into the wall: always a big one (this is what spins the ship)
		ship_spawn_impact_sparks(self, ship_nose(self), face->normal, 0.6 + self->speed / 30000.0);
	}
}


void ship_collide_with_track(ship_t *self, track_face_t *face) {
	float alpha;
	section_t 	*trackPtr;
	bool collide;
	track_face_t *face2;

	trackPtr = self->section->next;
	vec3_t direction = vec3_sub(trackPtr->center, self->section->center);
	float down_track = vec3_dot(direction, self->mat.basis.forward.vec3);

	if (down_track < 0) {
		flags_rm(self->flags, SHIP_DIRECTION_FORWARD);
	}
	else {
		flags_add(self->flags, SHIP_DIRECTION_FORWARD);
	}

	vec3_t to_face_vector = vec3_sub(face->tris[0].vertices[0].pos, face->tris[0].vertices[1].pos);
	direction = vec3_sub(self->section->center, self->position);
	float to_face = vec3_dot(direction, to_face_vector);

	face--;

	// Check against left hand side of track
	
	// FIXME: the collision checks in junctions are very flakey and often select
	// the wrong face to test for a collision.
	// Instead of this whole mess here, there should just be a function 
	// `track_get_nearest_face(section, pos)` that we call with the nose and 
	// wing positions and then just resolve against this face.

	if (to_face > 0) {
		flags_add(self->flags, SHIP_LEFT_SIDE);
		
		vec3_t face_point = face->tris[0].vertices[0].pos;

		alpha = vec3_distance_to_plane(ship_nose(self), face_point, face->normal);
		if (alpha <= 0) {
			if (flags_is(self->section->flags, SECTION_JUNCTION_START)) {
				collide = vec3_is_on_face(ship_nose(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, -down_track);
				}
				else {
					face2 = g.track.faces + self->section->next->face_start;
					collide = vec3_is_on_face(ship_nose(self), face2, alpha);
					if (collide) {
						ship_resolve_nose_collision(self, face, -down_track);
					}
				}
			}
			else if (flags_is(self->section->flags, SECTION_JUNCTION_END)) {
				collide = vec3_is_on_face(ship_nose(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, -down_track);
				}
				else {
					face2 = g.track.faces + self->section->prev->face_start;
					collide = vec3_is_on_face(ship_nose(self), face2, alpha);
					if (collide) {
						ship_resolve_nose_collision(self, face, -down_track);
					}
				}
			}
			else {
				ship_resolve_nose_collision(self, face, -down_track);
			}
			return;
		}

		alpha = vec3_distance_to_plane(ship_wing_left(self), face_point, face->normal);
		if (alpha <= 0) {
			if (
				flags_is(self->section->flags, SECTION_JUNCTION_START) || 
				flags_is(self->section->flags, SECTION_JUNCTION_END)
			) {
				collide = vec3_is_on_face(ship_wing_left(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, -down_track);
				}
			}
			else {
				ship_resolve_wing_collision(self, face, -down_track);
			}
			return;
		}

		alpha = vec3_distance_to_plane(ship_wing_right(self), face_point, face->normal);
		if (alpha <= 0) {
			if (
				flags_is(self->section->flags, SECTION_JUNCTION_START) || 
				flags_is(self->section->flags, SECTION_JUNCTION_END)
			) {
				collide = vec3_is_on_face(ship_wing_right(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, -down_track);
				}
			}
			else {
				ship_resolve_wing_collision(self, face, -down_track);
			}
			return;
		}
	}


	// Collision check against 2nd wall
	else {
		flags_rm(self->flags, SHIP_LEFT_SIDE);

		face++;
		while (face->flags & FACE_TRACK_BASE) {
			face++;
		}

		vec3_t face_point = face->tris[0].vertices[0].pos;

		alpha = vec3_distance_to_plane(ship_nose(self), face_point, face->normal);
		if (alpha <= 0) {
			if (flags_is(self->section->flags, SECTION_JUNCTION_START)) {
				collide = vec3_is_on_face(ship_nose(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, down_track);
				}
				else {
					face2 = g.track.faces + self->section->next->face_start;
					face2 += 3;
					collide = vec3_is_on_face(ship_nose(self), face2, alpha);
					if (collide) {
						ship_resolve_nose_collision(self, face, -down_track);
					}
				}
			}
			else if (flags_is(self->section->flags, SECTION_JUNCTION_END)) {
				collide = vec3_is_on_face(ship_nose(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, -down_track);
				}
				else {
					face2 = g.track.faces + self->section->prev->face_start;
					face2 += 3;
					collide = vec3_is_on_face(ship_nose(self), face2, alpha);
					if (collide) {
						ship_resolve_nose_collision(self, face2, -down_track);
					}
				}
			}
			else {
				ship_resolve_nose_collision(self, face, down_track);
			}
			return;
		}
		
		alpha = vec3_distance_to_plane(ship_wing_left(self), face_point, face->normal);
		if (alpha <= 0) {
			if (
				flags_is(self->section->flags, SECTION_JUNCTION_START) ||
				flags_is(self->section->flags, SECTION_JUNCTION_END)
			) {
				collide = vec3_is_on_face(ship_wing_left(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, down_track);
				}
			}
			else {
				ship_resolve_wing_collision(self, face, down_track);
			}
			return;
		}

		alpha = vec3_distance_to_plane(ship_wing_right(self), face_point, face->normal);
		if (alpha <= 0) {
			if (
				flags_is(self->section->flags, SECTION_JUNCTION_START) ||
				flags_is(self->section->flags, SECTION_JUNCTION_END)
			) {
				collide = vec3_is_on_face(ship_wing_right(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, down_track);
				}
			}
			else {
				ship_resolve_wing_collision(self, face, down_track);
			}
			return;
		}
	}
}


bool ship_intersects_ship(ship_t *self, ship_t *other) {
	// Get 4 points of collision model in world space
	vec3_t a = vec3_transform(other->collision_model->vertices[0], &other->mat);
	vec3_t b = vec3_transform(other->collision_model->vertices[1], &other->mat);
	vec3_t c = vec3_transform(other->collision_model->vertices[2], &other->mat);
	vec3_t d = vec3_transform(other->collision_model->vertices[3], &other->mat);

	vec3_t other_points[6] = {b, a, d, a, a, b};
	vec3_t other_lines[6] = {
		vec3_sub(c, b),
		vec3_sub(c, a),
		vec3_sub(c, d),
		vec3_sub(b, a),
		vec3_sub(d, a),
		vec3_sub(d, b)
	};


	Prm poly = {.primitive = other->collision_model->primitives};
	int primitives_len = other->collision_model->primitives_len;

	vec3_t p1, p2, p3;

	// for all 4 planes of the enemy ship
	for (int pi = 0; pi < primitives_len; pi++) {
		int16_t *indices;
		switch (poly.primitive->type) {
			case PRM_TYPE_F3:
				indices = poly.f3++->coords;  break;
			case PRM_TYPE_G3:
				indices = poly.g3++->coords;  break;
			case PRM_TYPE_FT3:
				indices = poly.ft3++->coords; break;
			case PRM_TYPE_GT3:
				indices = poly.gt3++->coords; break;
			default: die("Can't happen?");
		}
		p1 =  vec3_transform(self->collision_model->vertices[indices[0]], &self->mat);
		p2 =  vec3_transform(self->collision_model->vertices[indices[1]], &self->mat);
		p3 =  vec3_transform(self->collision_model->vertices[indices[2]], &self->mat);

		// Find polyGon line vectors
		vec3_t p1p2 = vec3_sub(p2, p1);
		vec3_t p1p3 = vec3_sub(p3, p1);

		// Find plane equations
		vec3_t plane1 = vec3_cross(p1p2, p1p3);

		for (int vi = 0; vi < 6; vi++) {
			float dp1 = vec3_dot(vec3_sub(p1, other_points[vi]), plane1);
			float dp2 = vec3_dot(other_lines[vi], plane1);
			
			if (dp2 != 0) {
				float norm = dp1 / dp2;

				if ((norm >= 0) && (norm <= 1)) {
					vec3_t term = vec3_mulf(other_lines[vi], norm);
					vec3_t res = vec3_add(term, other_points[vi]);

					vec3_t v0 = vec3_sub(p1, res);
					vec3_t v1 = vec3_sub(p2, res);
					vec3_t v2 = vec3_sub(p3, res);
					
					float angle =
						vec3_angle(v0, v1) +
						vec3_angle(v1, v2) +
						vec3_angle(v2, v0);

					if ((angle >= M_PI * 2 - M_PI * 0.1)) {
						return true;
					}
				}
			}
		}
	}
	return false;
}

void ship_collide_with_ship(ship_t *self, ship_t *other) {
	float distance = vec3_len(vec3_sub(self->position, other->position));

	// Do a quick distance check; if ships are far apart, remove the collision flag
	// and early out.
	if (distance > 960) {
		flags_rm(self->flags, SHIP_COLL);
		flags_rm(other->flags, SHIP_COLL);
		return;
	}

	// Ships are close, do a real collision test
	if (!ship_intersects_ship(self, other)) {
		return;
	}

	// Ships did collide, resolve

	vec3_t vc = vec3_divf(
		vec3_add(
			vec3_mulf(self->velocity, self->mass),
			vec3_mulf(other->velocity, other->mass)
		),
		self->mass + other->mass
	);

	vec3_t ship_react = vec3_mulf(vec3_sub(vc, self->velocity), 0.5); // >> 1
	vec3_t other_react = vec3_mulf(vec3_sub(vc, other->velocity), 0.5); // >> 1
	self->position = vec3_sub(self->position, vec3_mulf(self->velocity, 0.015625)); // >> 6
	other->position = vec3_sub(other->position, vec3_mulf(other->velocity, 0.015625)); // >> 6

	self->velocity = vec3_add(vc, ship_react);
	other->velocity = vec3_add(vc, other_react);

	vec3_t res = vec3_sub(self->position, other->position);

	self->velocity = vec3_add(self->velocity, vec3_mulf(res, 4));  // << 2
	self->position = vec3_add(self->position, vec3_mulf(self->velocity, 0.015625)); // >> 6

	other->velocity = vec3_sub(other->velocity, vec3_mulf(res, 4)); // << 2
	other->position = vec3_add(other->position, vec3_mulf(other->velocity, 0.015625)); // >> 6

	if (
		flags_not(self->flags, SHIP_COLL) && 
		flags_not(other->flags, SHIP_COLL) &&
		self->last_impact_time > 0.2
	) {
		self->last_impact_time = 0;
		vec3_t sound_pos = vec3_mulf(vec3_add(self->position, other->position), 0.5);
		sfx_play_at(SFX_CRUNCH, sound_pos, vec3(0, 0, 0), 1);
		vec3_t away = vec3_sub(self->position, other->position);
		float relative_speed = vec3_len(vec3_sub(self->velocity, other->velocity));
		ship_spawn_impact_sparks(self, sound_pos, vec3_normalize(away), 0.2 + relative_speed / 12000.0);
	}
	flags_add(self->flags, SHIP_COLL);
	flags_add(other->flags, SHIP_COLL);
}
