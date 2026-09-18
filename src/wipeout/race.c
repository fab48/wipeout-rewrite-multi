#include "../input.h"
#include "../system.h"
#include "../utils.h"
#include "../render.h"

#include "track.h"
#include "ship.h"
#include "weapon.h"
#include "droid.h"
#include "camera.h"
#include "scene.h"
#include "game.h"
#include "settings.h"
#include "hud.h"
#include "ui.h"
#include "sfx.h"
#include "race.h"
#include "particle.h"
#include "menu.h"
#include "ingame_menus.h"

#define ATTRACT_DURATION 60.0

static bool is_paused = false;
static bool menu_is_scroll_text = false;
static bool has_show_credits = false;
static float attract_start_time;
static menu_t *active_menu = NULL;

// Selects the camera and the part of the screen for this player
static void race_set_player_view(int player, vec2i_t screen) {
	g.view_player = player;
	g.camera = &g.cameras[player];

	if (g.num_players > 1) {
		if (settings.split_vertical) {
			// Side by side
			int half = screen.x / 2;
			if (player == 0) {
				render_set_viewport(vec2i(0, 0), vec2i(half, screen.y));
			}
			else {
				render_set_viewport(vec2i(half, 0), vec2i(screen.x - half, screen.y));
			}
		}
		else {
			// Top / bottom
			int half = screen.y / 2;
			if (player == 0) {
				render_set_viewport(vec2i(0, 0), vec2i(screen.x, half));
			}
			else {
				render_set_viewport(vec2i(0, half), vec2i(screen.x, screen.y - half));
			}
		}
	}
}

// Point lights: one per ship at its exhaust, plus short flashes for the
// explosions. Only the closest few within LIGHTS_MAX_DISTANCE of the camera
// are used. This is an option (POINT LIGHTS in the video options) and needs
// the PBR lighting to have any effect.
#define LIGHTS_MAX_ACTIVE RENDER_LIGHTS_MAX
#define LIGHTS_MAX_DISTANCE 14000.0
#define FLASH_LIGHTS_MAX 8
#define FLASH_LIGHT_DURATION 0.6

typedef struct {
	vec3_t pos;
	vec3_t color;
	float timer;
} flash_light_t;

static flash_light_t flash_lights[FLASH_LIGHTS_MAX];

void race_add_flash_light(vec3_t pos, int particle_type) {
	vec3_t color;
	switch (particle_type) {
	case PARTICLE_TYPE_FIRE:       color = vec3(11.0, 5.5, 1.8); break;
	case PARTICLE_TYPE_FIRE_WHITE: color = vec3(10.0, 9.0, 7.0); break;
	case PARTICLE_TYPE_EBOLT:
	case PARTICLE_TYPE_GREENY:     color = vec3(2.5, 10.0, 3.5); break;
	default:                       color = vec3(7.5, 7.5, 7.5); break;
	}

	// Take a free slot, or the one that is closest to expiring
	int slot = 0;
	for (int i = 0; i < FLASH_LIGHTS_MAX; i++) {
		if (flash_lights[i].timer < flash_lights[slot].timer) {
			slot = i;
		}
	}
	flash_lights[slot] = (flash_light_t){.pos = pos, .color = color, .timer = FLASH_LIGHT_DURATION};
}

static void race_update_flash_lights(void) {
	for (int i = 0; i < FLASH_LIGHTS_MAX; i++) {
		if (flash_lights[i].timer > 0) {
			flash_lights[i].timer -= system_tick();
		}
	}
}

static void race_lights_insert(render_light_t *lights, float *distances, int *lights_len, render_light_t light, float max_distance) {
	float distance = vec3_len(vec3_sub(light.pos, g.camera->position));
	if (distance > max_distance + light.radius) {
		return;
	}

	// Insert sorted by distance
	int j = *lights_len;
	while (j > 0 && distances[j - 1] > distance) {
		lights[j] = lights[j - 1];
		distances[j] = distances[j - 1];
		j--;
	}
	lights[j] = light;
	distances[j] = distance;
	(*lights_len)++;
}

static void race_set_exhaust_lights(void) {
	// The POINT LIGHTS video option: how many lights at most, 0 = off
	int max_active = (render_get_post_effect() & RENDER_POST_POINT_LIGHTS_MASK) >> RENDER_POST_POINT_LIGHTS_SHIFT;
	max_active = min(max_active, RENDER_LIGHTS_MAX);
	if (max_active == 0) {
		render_set_lights(NULL, 0);
		return;
	}

	render_light_t lights[LIGHTS_MAX_ACTIVE + len(g.ships) + FLASH_LIGHTS_MAX];
	float distances[LIGHTS_MAX_ACTIVE + len(g.ships) + FLASH_LIGHTS_MAX];
	int lights_len = 0;

	// Explosions first: they always get a slot, the exhausts only fill up 
	// what is left
	for (int i = 0; i < FLASH_LIGHTS_MAX; i++) {
		flash_light_t *flash = &flash_lights[i];
		if (flash->timer <= 0) {
			continue;
		}
		float intensity = flash->timer / FLASH_LIGHT_DURATION;
		race_lights_insert(lights, distances, &lights_len, (render_light_t){
			.pos = flash->pos,
			.color = vec3_mulf(flash->color, intensity),
			.radius = 9000,
			.per_pixel = false
		}, RENDER_FADEOUT_FAR); // explosions are rare: no distance limit
	}
	lights_len = min(lights_len, max_active);

	int flashes_len = lights_len;
	render_light_t exhausts[len(g.ships)];
	float exhaust_distances[len(g.ships)];
	int exhausts_len = 0;

	for (int i = 0; i < len(g.ships); i++) {
		vec3_t pos;
		float intensity;
		if (!ship_exhaust_light(&g.ships[i], &pos, &intensity)) {
			continue;
		}
		race_lights_insert(exhausts, exhaust_distances, &exhausts_len, (render_light_t){
			.pos = pos,
			.color = vec3(0.35 * intensity, 0.6 * intensity, 1.4 * intensity),
			.radius = 3000,
			.per_pixel = true
		}, LIGHTS_MAX_DISTANCE);
	}
	for (int i = 0; i < exhausts_len && lights_len < max_active; i++) {
		lights[lights_len++] = exhausts[i];
	}

	render_set_lights(lights, lights_len);
	(void)flashes_len;
}

void race_init(void) {
	ingame_menus_load();
	menu_is_scroll_text = false;

	const circuit_settings_t *cs = &def.circuits[g.circuit].settings[g.race_class];
	track_load(cs->path);
	scene_load(cs->path, cs->sky_y_offset);
	scene_render_sky_env();
	
	if (g.circuit == CIRCUIT_SILVERSTREAM && g.race_class == RACE_CLASS_RAPIER) {
		scene_init_aurora_borealis();	
	} 

	if (g.is_attract_mode) {
		g.pilot = rand_int(0, len(def.pilots));
		g.num_players = 1;
		g.duel = false;
	}
	race_start();
	// render_textures_dump("texture_atlas.png");

	if (g.is_attract_mode) {
		attract_start_time = system_time();

		for (int i = 0; i < len(g.ships); i++) {
			flags_rm(g.ships[i].flags, SHIP_VIEW_INTERNAL);
			flags_rm(g.ships[i].flags, SHIP_RACING);
		}

		g.cameras[0].update_func = camera_update_attract_random;
		if (!has_show_credits || rand_int(0, 10) == 0) {
			active_menu = text_scroll_menu_init(def.credits, len(def.credits));
			menu_is_scroll_text = true;
			has_show_credits = true;
		}
	}

	is_paused = false;
}

void race_update(void) {
	if (is_paused) {
		if (!active_menu) {
			active_menu = pause_menu_init();
		}
		if (input_pressed(A_MENU_QUIT)) {
			race_unpause();
		}
	}
	else {
		g.view_player = 0;
		g.camera = &g.cameras[0];
		ships_update();
		for (int p = 0; p < g.num_players; p++) {
			ship_t *ship = game_player_ship(p);
			droid_update(&g.droids[p], ship);
			camera_update(&g.cameras[p], ship, &g.droids[p]);
		}
		weapons_update();
		particles_update();
		race_update_flash_lights();
		scene_update();
		if (g.race_type != RACE_TYPE_TIME_TRIAL) {
			track_cycle_pickups();
		}

		if (g.is_attract_mode) {
			if (input_pressed(A_MENU_START) || input_pressed(A_MENU_SELECT)) {
				game_set_scene(GAME_SCENE_MAIN_MENU);
			}
			float duration = system_time() - attract_start_time;
			if ((!active_menu && duration > 30) || duration > 120) {
				game_set_scene(GAME_SCENE_TITLE);
			}
		}
		else if (active_menu == NULL && (input_pressed(A_MENU_START) || input_pressed(A_MENU_QUIT))) {
			race_pause();
		}
	}


	// Draw 3D; once for each player
	render_reset_viewport();
	vec2i_t screen = render_size();

	for (int p = 0; p < g.num_players; p++) {
		race_set_player_view(p, screen);
		render_set_view(g.camera->position, g.camera->angle);
		render_set_screen_position(g.camera->shake);
		race_set_exhaust_lights();

		render_set_cull_backface(false);
		scene_draw(g.camera);
		track_draw(g.camera);
		render_set_cull_backface(true);

		ships_draw();
		for (int d = 0; d < g.num_players; d++) {
			droid_draw(&g.droids[d]);
		}
		weapons_draw();
		render_set_material(RENDER_MATERIAL_UNLIT);
		particles_draw();
	}

	// Speed dependent motion blur and bloom; applied before the HUD is drawn.
	// The radial motion blur only makes sense for a single, centered view.
	race_set_player_view(0, screen);
	render_reset_viewport();
	render_set_screen_position(vec2(0,0));
	float motion_blur = g.num_players == 1
		? clamp((g.ships[g.pilot].speed - 5000.0) / 20000.0, 0.0, 1.0)
		: 0;
	render_scene_post(motion_blur);

	// Draw 2d; with a smaller HUD for the half height views in split screen
	int ui_scale = ui_get_scale();
	if (g.num_players > 1 && !settings.split_vertical) {
		ui_set_scale(max(1, (ui_scale + 1) / 2));
	}

	for (int p = 0; p < g.num_players; p++) {
		race_set_player_view(p, screen);
		if (g.num_players > 1) {
			// The HUD needs the 3d view of this player for the target reticle
			render_set_view(g.camera->position, g.camera->angle);
		}
		render_set_view_2d();

		ship_t *ship = game_player_ship(p);
		if (flags_is(ship->flags, SHIP_RACING)) {
			hud_draw(ship);
			if (g.num_players > 1) {
				hud_draw_player_marker(game_player_ship(1 - p), p == 0 ? "P2" : "P1");
			}
		}
		else if (g.num_players > 1 && !g.is_attract_mode && ship->lap >= NUM_LAPS) {
			ui_draw_text_centered("FINISHED", ui_scaled_pos(UI_POS_MIDDLE | UI_POS_CENTER, vec2i(0, -24)), UI_SIZE_16, UI_COLOR_ACCENT);
			ui_draw_text_centered("POSITION", ui_scaled_pos(UI_POS_MIDDLE | UI_POS_CENTER, vec2i(-12, 0)), UI_SIZE_12, UI_COLOR_DEFAULT);
			ui_draw_number(g.finish_rank[p], ui_scaled_pos(UI_POS_MIDDLE | UI_POS_CENTER, vec2i(44, 0)), UI_SIZE_12, UI_COLOR_DEFAULT);
		}
	}

	ui_set_scale(ui_scale);
	race_set_player_view(0, screen);
	render_reset_viewport();
	render_set_view_2d();

	if (g.num_players > 1) {
		// Divider between the two views
		int thickness = max(2, screen.y / 270);
		if (settings.split_vertical) {
			render_push_2d(vec2i(screen.x / 2 - thickness / 2, 0), vec2i(thickness, screen.y), rgba(0, 0, 0, 255), RENDER_NO_TEXTURE);
		}
		else {
			render_push_2d(vec2i(0, screen.y / 2 - thickness / 2), vec2i(screen.x, thickness), rgba(0, 0, 0, 255), RENDER_NO_TEXTURE);
		}
	}

	if (g.is_attract_mode && !active_menu) {
		ui_draw_text("DEMO MODE", ui_scaled_pos(UI_POS_TOP | UI_POS_CENTER, vec2i(-56, 24)), UI_SIZE_8, UI_COLOR_ACCENT);
	}

	if (active_menu) {
		if (!menu_is_scroll_text) {
			vec2i_t size = render_size();
			render_push_2d(vec2i(0, 0), size, rgba(0, 0, 0, 128), RENDER_NO_TEXTURE);
		}
		menu_update(active_menu);
	}
}

void race_start(void) {
	active_menu = NULL;
	sfx_reset();
	scene_init();
	g.view_player = 0;
	g.camera = &g.cameras[0];
	if (g.num_players > 1 && g.pilot2 == g.pilot) {
		g.pilot2 = (g.pilot + 1) % len(def.pilots);
	}
	ships_init(g.track.sections);
	for (int p = 0; p < g.num_players; p++) {
		camera_init(&g.cameras[p], g.track.sections);
		g.cameras[p].update_func = camera_update_race_intro;
		droid_init(&g.droids[p], game_player_ship(p));
	}
	particles_init();
	weapons_init();

	for (int i = 0; i < len(g.race_ranks); i++) {
		g.race_ranks[i].points = 0;
		g.race_ranks[i].pilot = i;
	}
	for (int i = 0; i < len(g.lap_times); i++) {
		for (int j = 0; j < len(g.lap_times[i]); j++) {
			g.lap_times[i][j] = 0;
		}
	}
	g.is_new_race_record = false;
	g.is_new_lap_record = false;
	g.best_lap = 0;
	g.race_time = 0;
}

void race_restart(void) {
	race_unpause();

	if (g.race_type == RACE_TYPE_CHAMPIONSHIP) {
		g.lives--;
		if (g.lives == 0) {
			race_release_control();
			active_menu = game_over_menu_init();
			return;
		}
	}

	race_start();
}

static bool sort_points_compare(pilot_points_t *pa, pilot_points_t *pb) {
	return (pa->points < pb->points);
}

void race_end(void) {
	race_release_control();

	g.race_position = g.ships[g.pilot].position_rank;

	g.race_time = 0;
	g.best_lap = g.lap_times[g.pilot][0];
	for (int i = 0; i < NUM_LAPS; i++) {
		g.race_time += g.lap_times[g.pilot][i];
		if (g.lap_times[g.pilot][i] < g.best_lap) {
			g.best_lap = g.lap_times[g.pilot][i];
		}
	}

	// No records in split screen
	highscores_t *hs = &save.highscores[g.race_class][g.circuit][g.highscore_tab];
	if (g.num_players == 1 && g.best_lap < hs->lap_record) {
		hs->lap_record = g.best_lap;
		g.is_new_lap_record = true;
		save.is_dirty = true;
	}

	for (int i = 0; g.num_players == 1 && i < NUM_HIGHSCORES; i++) {
		if (g.race_time < hs->entries[i].time) {
			g.is_new_race_record = true;
			break;
		}
	}

	if (g.race_type == RACE_TYPE_CHAMPIONSHIP) {
		for (int i = 0; i < len(def.race_points_for_rank); i++) {
			g.race_ranks[i].points = def.race_points_for_rank[i];

			// Find the pilot for this race rank in the championship table
			for (int j = 0; j < len(g.championship_ranks); j++) {
				if (g.race_ranks[i].pilot == g.championship_ranks[j].pilot) {
					g.championship_ranks[j].points += def.race_points_for_rank[i];
					break;
				}
			}
		}
		sort(g.championship_ranks, len(g.championship_ranks), sort_points_compare);
	}

	active_menu = race_stats_menu_init();
}

void race_next(void) {
	int next_circuit = g.circuit + 1;

	// Championship complete
	if (
		(save.has_bonus_circuits && next_circuit >= NUM_WIPEOUT_CIRCUITS) ||
		(!save.has_bonus_circuits && next_circuit >= NUM_NON_BONUS_CIRCUITS)
	) {
		if (g.race_class == RACE_CLASS_RAPIER) {
			if (save.has_bonus_circuits) {
				active_menu = text_scroll_menu_init(def.congratulations.rapier_all_circuits, len(def.congratulations.rapier_all_circuits));
			}
			else {
				save.has_bonus_circuits = true;
				active_menu = text_scroll_menu_init(def.congratulations.rapier, len(def.congratulations.rapier));
			}
		}
		else {
			save.has_rapier_class = true;
			if (save.has_bonus_circuits) {
				active_menu = text_scroll_menu_init(def.congratulations.venom_all_circuits, len(def.congratulations.venom_all_circuits));
			}
			else {
				active_menu = text_scroll_menu_init(def.congratulations.venom, len(def.congratulations.venom));
			}
		}
		save.is_dirty = true;
		menu_is_scroll_text = true;
	}

	// Next track
	else {
		g.circuit = next_circuit;
		game_set_scene(GAME_SCENE_RACE);
	}
}

static void race_release_ship(ship_t *ship) {
	flags_rm(ship->flags, SHIP_RACING);
	ship->remote_thrust_max = 3160;
	ship->remote_thrust_mag = 32;
	ship->speed = 3160;
	game_ship_camera(ship)->update_func = camera_update_attract_random;
}

void race_release_control(void) {
	for (int p = 0; p < g.num_players; p++) {
		race_release_ship(game_player_ship(p));
	}
}

// Called when a player crosses the finish line after the last lap. In split
// screen the race goes on until everybody is done.
void race_player_finished(ship_t *ship) {
	g.finish_rank[ship->player] = ship->position_rank;

	// Split screen: the first player over the line wins, the race is over for
	// both. race_end() releases the loser too: autopilot and cinematic camera.
	if (g.num_players > 1) {
		for (int p = 0; p < g.num_players; p++) {
			ship_t *other = game_player_ship(p);
			if (other != ship) {
				g.finish_rank[p] = other->position_rank;
			}
		}
	}
	race_end();
}

void race_pause(void) {
	sfx_pause();
	is_paused = true;
}

void race_unpause(void) {
	sfx_unpause();
	is_paused = false;
	active_menu = NULL;
}
