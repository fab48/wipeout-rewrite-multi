#include <string.h>
#include <ctype.h>

#include "../net.h"
#include "../input.h"
#include "../system.h"
#include "../utils.h"
#include "../mem.h"
#include "../platform.h"

#include "netplay.h"
#include "game.h"
#include "race.h"
#include "ship.h"
#include "ship_player.h"
#include "weapon.h"
#include "droid.h"
#include "camera.h"
#include "particle.h"
#include "scene.h"
#include "track.h"
#include "sfx.h"
#include "ui.h"
#include "menu.h"
#include "settings.h"
#include "hud.h"
#include "../render.h"

net_session_t net;

static struct {
	bool initialized;
	char name[NET_NAME_LEN];
	uint16_t port;
	char status_text[64];

	// Command line, for tests and LAN parties without broadcast
	bool bot;
	bool cmd_host;
	char cmd_join[128];
	int cmd_autostart;      // host: start as soon as this many players are in
	int cmd_circuit;
	int cmd_race_class;
	int cmd_races;          // quit after this many races (0 = never)
	double cmd_quit_time;   // quit after this many seconds (0 = never)
	int races_done;
	bool cmd_print_stats;
	bool return_to_lan;

	// Race state
	uint16_t loaded_race_id;
	bool race_running;      // host: the simulation has been started
	double first_finish_time;
	bool results_shown;
	double last_snapshot_time;
	droid_t remote_droids[NET_MAX_PLAYERS];
	menu_t *menu;

	// Events, host
	struct { net_event_t ev; double time; } events[128];
	uint32_t event_seq;

	// Client
	uint32_t last_event_seq;
	bool have_events;
	bool view_internal;
	bool finished_shown;
	float bot_fire_timer;
} np;

#define NETPLAY_EVENT_WINDOW 0.35


// -----------------------------------------------------------------------------
// Setup

static void netplay_default_name(char *name) {
	const char *env = getenv("WIPEOUT_NAME");
	if (!env) { env = getenv("COMPUTERNAME"); }
	if (!env) { env = getenv("HOSTNAME"); }
	if (!env) { env = getenv("USER"); }
	if (!env) { env = "PLAYER"; }
	net_sanitize_name(name, env);
}

// lan.txt next to save.dat: "name = FAB", "join = 192.168.1.20", "port = 47800"
static void netplay_load_config(void) {
	uint32_t size = 0;
	uint8_t *bytes = platform_load_userdata("lan.txt", &size);
	if (!bytes) {
		return;
	}
	char *text = mem_temp_alloc(size + 1);
	memcpy(text, bytes, size);
	text[size] = '\0';
	mem_temp_free(bytes);

	char *line = text;
	while (line && *line) {
		char *next = strchr(line, '\n');
		if (next) {
			*next++ = '\0';
		}
		char key[32], value[128];
		if (sscanf(line, " %31[a-z_] = %127s", key, value) == 2) {
			if (strcmp(key, "name") == 0) {
				net_sanitize_name(np.name, value);
			}
			else if (strcmp(key, "join") == 0) {
				snprintf(np.cmd_join, sizeof(np.cmd_join), "%s", value);
			}
			else if (strcmp(key, "port") == 0) {
				int port = atoi(value);
				if (port > 0 && port < 65536) {
					np.port = port;
				}
			}
		}
		line = next;
	}
	mem_temp_free(text);
}

void netplay_init(int argc, char **argv) {
	memset(&np, 0, sizeof(np));
	np.port = NET_DEFAULT_PORT;
	np.cmd_circuit = -1;
	np.cmd_race_class = -1;
	netplay_default_name(np.name);
	net_session_init(&net);
	netplay_load_config();

	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;
		if (strcmp(arg, "--host") == 0) {
			np.cmd_host = true;
		}
		else if (strcmp(arg, "--join") == 0 && val) {
			snprintf(np.cmd_join, sizeof(np.cmd_join), "%s", val);
			i++;
		}
		else if (strcmp(arg, "--name") == 0 && val) {
			net_sanitize_name(np.name, val);
			i++;
		}
		else if (strcmp(arg, "--pilot") == 0 && val) {
			settings.net_pilot = clamp(atoi(val), 0, NUM_PILOTS - 1);
			i++;
		}
		else if (strcmp(arg, "--port") == 0 && val) {
			np.port = clamp(atoi(val), 1, 65535);
			i++;
		}
		else if (strcmp(arg, "--circuit") == 0 && val) {
			np.cmd_circuit = clamp(atoi(val), 0, NUM_WIPEOUT_CIRCUITS - 1);
			i++;
		}
		else if (strcmp(arg, "--class") == 0 && val) {
			np.cmd_race_class = clamp(atoi(val), 0, NUM_RACE_CLASSES - 1);
			i++;
		}
		else if (strcmp(arg, "--autostart") == 0 && val) {
			np.cmd_autostart = clamp(atoi(val), 1, NET_MAX_PLAYERS);
			i++;
		}
		else if (strcmp(arg, "--races") == 0 && val) {
			np.cmd_races = max(0, atoi(val));
			i++;
		}
		else if (strcmp(arg, "--quit-after") == 0 && val) {
			np.cmd_quit_time = atof(val);
			i++;
		}
		else if (strcmp(arg, "--bot") == 0) {
			np.bot = true;
		}
		else if (strcmp(arg, "--stats") == 0) {
			np.cmd_print_stats = true;
		}
		else if (strcmp(arg, "--net-loss") == 0 && val) {
			net.sim.loss = clamp(atof(val), 0.0, 0.9);
			i++;
		}
		else if (strcmp(arg, "--net-dup") == 0 && val) {
			net.sim.duplicate = clamp(atof(val), 0.0, 0.9);
			i++;
		}
	}
	np.initialized = true;
}

void netplay_cleanup(void) {
	if (netplay_active()) {
		net_session_close(&net);
	}
	net_cleanup();
}

bool netplay_active(void) {
	return net.role == NET_ROLE_HOST || net.role == NET_ROLE_CLIENT;
}

bool netplay_is_host(void) {
	return net.role == NET_ROLE_HOST;
}

bool netplay_is_client(void) {
	return net.role == NET_ROLE_CLIENT;
}

bool netplay_bot_enabled(void) {
	return np.bot;
}

static void netplay_print_stats(const char *when) {
	net_stats_t *st = &net.stats;
	printf(
		"net stats (%s) %s: sent %u pkts / %u KB, received %u pkts / %u KB, malformed %u, "
		"snapshots %u, late %u, missed %u, max gap %.0f ms, dropped slots %u\n",
		when, net.role == NET_ROLE_HOST ? "host" : "client",
		st->packets_sent, st->bytes_sent / 1024, st->packets_received, st->bytes_received / 1024,
		st->malformed, st->snapshots, st->snapshots_late, st->snapshots_missed,
		st->snapshot_max_gap * 1000.0, st->slots_dropped
	);
	fflush(stdout);
}

bool netplay_host_session(void) {
	int race_class = np.cmd_race_class >= 0 ? np.cmd_race_class : g.race_class;
	int circuit = np.cmd_circuit >= 0 ? np.cmd_circuit : (g.circuit < NUM_WIPEOUT_CIRCUITS ? g.circuit : 0);
	g.pilot = settings.net_pilot;
	if (!net_session_host(&net, np.port, np.name, g.pilot, race_class, circuit)) {
		snprintf(np.status_text, sizeof(np.status_text), "CAN'T OPEN PORT %d", np.port);
		return false;
	}
	np.status_text[0] = '\0';
	printf("net: hosting on port %d as %s\n", np.port, np.name);
	return true;
}

bool netplay_discover(void) {
	if (!net_session_discover(&net, np.port)) {
		snprintf(np.status_text, sizeof(np.status_text), "NETWORK ERROR");
		return false;
	}
	np.status_text[0] = '\0';
	return true;
}

bool netplay_join(net_addr_t addr) {
	g.pilot = settings.net_pilot;
	if (!net_session_join(&net, addr, np.name, g.pilot)) {
		snprintf(np.status_text, sizeof(np.status_text), "NETWORK ERROR");
		return false;
	}
	char buf[32];
	printf("net: joining %s as %s\n", net_addr_to_string(addr, buf, sizeof(buf)), np.name);
	np.status_text[0] = '\0';
	return true;
}

void netplay_leave(const char *reason) {
	if (net.role != NET_ROLE_NONE) {
		if (np.cmd_print_stats && netplay_active()) {
			netplay_print_stats("leave");
		}
		net_session_close(&net);
	}
	if (reason) {
		snprintf(np.status_text, sizeof(np.status_text), "%s", reason);
		printf("net: %s\n", reason);
		np.return_to_lan = true;
	}
	np.loaded_race_id = 0;
	np.race_running = false;
	g.num_players = 1;
}

const char *netplay_status_text(void) {
	return np.status_text;
}

bool netplay_take_return_to_lan(void) {
	bool r = np.return_to_lan;
	np.return_to_lan = false;
	return r;
}

void netplay_set_pilot(int pilot) {
	g.pilot = pilot;
	settings.net_pilot = pilot;
	settings_set_dirty();
	if (netplay_is_host()) {
		net_session_host_set_pilot(&net, pilot);
	}
	else if (netplay_is_client()) {
		net_session_client_set_pilot(&net, pilot);
	}
}

void netplay_host_start_race(void) {
	if (!netplay_is_host() || net.phase != NET_PHASE_LOBBY) {
		return;
	}
	net_session_host_start_loading(&net);
	printf("net: starting race %d, circuit %d, %d players\n", net.race_id, net.circuit, net_session_num_players(&net));
}


// -----------------------------------------------------------------------------
// Following the session around: lobby -> race -> results -> lobby

static void netplay_enter_race(void) {
	int my_slot = net.my_slot;
	g.race_class = net.race_class;
	g.circuit = net.circuit;
	g.race_type = RACE_TYPE_SINGLE;
	g.highscore_tab = HIGHSCORE_TAB_RACE;
	g.num_players = 1;
	g.duel = false;
	g.is_attract_mode = false;
	if (my_slot >= 0) {
		g.pilot = net.slots[my_slot].pilot;
	}
	np.loaded_race_id = net.race_id;
	np.race_running = false;
	game_set_scene(GAME_SCENE_RACE);
}

void netplay_update(void) {
	if (!np.initialized) {
		return;
	}

	// Command line: start a session right away and skip the intro
	if (np.cmd_host || np.cmd_join[0]) {
		if (np.cmd_host) {
			netplay_host_session();
		}
		else {
			net_addr_t addr;
			if (net_addr_parse(np.cmd_join, np.port, &addr)) {
				netplay_join(addr);
			}
			else {
				snprintf(np.status_text, sizeof(np.status_text), "BAD ADDRESS %.40s", np.cmd_join);
			}
		}
		np.cmd_host = false;
		np.cmd_join[0] = '\0';
		game_set_scene(GAME_SCENE_MAIN_MENU);
	}

	if (np.cmd_quit_time > 0 && system_time() > np.cmd_quit_time) {
		printf("net: quit after %.0f seconds\n", np.cmd_quit_time);
		netplay_leave(NULL);
		system_exit();
		np.cmd_quit_time = 0;
	}

	if (net.role == NET_ROLE_NONE) {
		return;
	}

	// The client's input goes out with the next client state
	if (netplay_is_client()) {
		net_input_t *in = &net.input;
		// Only once the race scene is up: before that the ships point into a
		// track that isn't loaded
		bool in_race = game_get_current_scene() == GAME_SCENE_RACE && !race_menu_is_open();
		for (int a = 0; a < NET_NUM_ACTIONS; a++) {
			float state = 0;
			bool pressed = false;
			if (np.bot && in_race) {
				ship_t *ship = &g.ships[g.pilot];
				state = netplay_input_state(ship, a);
				pressed = netplay_input_pressed(ship, a);
			}
			else if (in_race) {
				state = input_state(a);
				pressed = input_pressed(a);
			}
			in->analog[a] = (uint8_t)(clamp(state, 0.0f, 1.0f) * 255.0f + 0.5f);
			if (pressed) {
				in->presses[a]++;
			}
		}
		in->analog_response = (uint8_t)clamp(save.analog_response * 20.0f, 0.0f, 255.0f);
	}

	net_session_update(&net, net_time());

	// Soak tests: where is our ship?
	static double next_progress_log = 0;
	if (np.cmd_print_stats && game_get_current_scene() == GAME_SCENE_RACE && net.now > next_progress_log) {
		next_progress_log = net.now + 5.0;
		ship_t *ship = &g.ships[g.pilot];
		printf(
			"net progress %s: phase %d lap %d section %d rank %d speed %.0f flags %x snapshots %u missed %u\n",
			np.name, net.phase, ship->lap, ship->section ? ship->section->num : -1,
			ship->position_rank, ship->speed, ship->flags, net.stats.snapshots, net.stats.snapshots_missed
		);
	}

	if (netplay_is_host()) {
		netplay_host_auto_continue();
		if (
			np.cmd_autostart && net.phase == NET_PHASE_LOBBY &&
			game_get_scene() == GAME_SCENE_MAIN_MENU &&
			net_session_num_players(&net) >= np.cmd_autostart &&
			net.now - net.phase_time > 1.0
		) {
			netplay_host_start_race();
		}
		if (net.phase == NET_PHASE_LOADING && np.loaded_race_id != net.race_id) {
			netplay_enter_race();
		}
		return;
	}

	if (netplay_is_client()) {
		switch (net.status) {
			case NET_STATUS_REJECTED:
				netplay_leave(
					net.reject_reason == NET_REJECT_FULL ? "THE GAME IS FULL" :
					net.reject_reason == NET_REJECT_IN_RACE ? "RACE IN PROGRESS, TRY AGAIN LATER" :
					"VERSION MISMATCH"
				);
				game_set_scene(GAME_SCENE_MAIN_MENU);
				return;
			case NET_STATUS_HOST_LOST:
				// Never got an answer: wrong address, firewall, or a host
				// with another version of the game (it ignores our packets)
				netplay_leave(net.session == 0 ? "NO ANSWER FROM THE HOST" : "CONNECTION TO THE HOST LOST");
				game_set_scene(GAME_SCENE_MAIN_MENU);
				return;
			case NET_STATUS_HOST_LEFT:
				netplay_leave("THE HOST ENDED THE GAME");
				game_set_scene(GAME_SCENE_MAIN_MENU);
				if (np.cmd_races) {
					system_exit(); // soak test: we're done
				}
				return;
			default:
				break;
		}
		if (net.status != NET_STATUS_OK) {
			return;
		}

		bool in_race_scene = game_get_scene() == GAME_SCENE_RACE;
		if ((net.phase == NET_PHASE_LOADING || net.phase == NET_PHASE_RACE) && np.loaded_race_id != net.race_id) {
			netplay_enter_race();
		}
		else if (net.phase == NET_PHASE_LOBBY && in_race_scene) {
			game_set_scene(GAME_SCENE_MAIN_MENU);
		}
		else if (net.phase == NET_PHASE_RESULTS && in_race_scene && !np.results_shown) {
			if (np.cmd_print_stats) {
				netplay_print_stats("race");
			}
			np.results_shown = true;
			race_show_menu(netplay_results_menu_init());
		}
	}
}


// -----------------------------------------------------------------------------
// Ships and input

int netplay_player_for_pilot(int pilot) {
	int slot = net_session_slot_for_pilot(&net, pilot);
	if (slot < 0) {
		return -1;
	}
	if (slot == net.my_slot) {
		return 0;
	}
	return NETPLAY_REMOTE_PLAYER + slot;
}

static net_slot_t *netplay_remote_slot(ship_t *ship) {
	int slot = ship->player - NETPLAY_REMOTE_PLAYER;
	if (slot < 0 || slot >= NET_MAX_PLAYERS) {
		return NULL;
	}
	return &net.slots[slot];
}

// A very simple autopilot for the soak tests: full thrust, steer towards the
// track a few sections ahead, fire whatever we picked up
static float netplay_bot_input(ship_t *ship, int action) {
	if (!ship->section) {
		return 0;
	}
	section_t *target = ship->section->next->next->next;
	vec3_t dir = vec3_sub(target->center, ship->position);
	float desired = -atan2f(dir.x, dir.z);
	float diff = wrap_angle(desired - ship->angle.y);
	switch (action) {
		case A_THRUST: return 1;
		case A_LEFT: return diff > 0.02 ? clamp(diff * 3.0f, 0.0f, 1.0f) : 0;
		case A_RIGHT: return diff < -0.02 ? clamp(-diff * 3.0f, 0.0f, 1.0f) : 0;
		case A_BRAKE_LEFT: return diff > 0.5 ? 1 : 0;
		case A_BRAKE_RIGHT: return diff < -0.5 ? 1 : 0;
		default: return 0;
	}
}

float netplay_input_state(ship_t *ship, int action) {
	if (ship_is_remote_player(ship)) {
		net_slot_t *slot = netplay_remote_slot(ship);
		if (!slot || !slot->connected || action < 0 || action >= NET_NUM_ACTIONS) {
			return 0;
		}
		return slot->input.analog[action] / 255.0f;
	}
	if (np.bot && ship->player == 0) {
		return netplay_bot_input(ship, action);
	}
	if (netplay_active() && race_menu_is_open()) {
		return 0;
	}
	return input_state(action + (ship->player == 1 ? A_P2_UP : 0));
}

bool netplay_input_pressed(ship_t *ship, int action) {
	if (ship_is_remote_player(ship)) {
		net_slot_t *slot = netplay_remote_slot(ship);
		if (!slot || action < 0 || action >= NET_NUM_ACTIONS) {
			return false;
		}
		return slot->pressed[action];
	}
	if (np.bot && ship->player == 0) {
		if (action == A_FIRE && ship->weapon_type != WEAPON_TYPE_NONE) {
			np.bot_fire_timer += system_tick();
			if (np.bot_fire_timer > 1.5) {
				np.bot_fire_timer = 0;
				return true;
			}
		}
		return false;
	}
	if (netplay_active() && race_menu_is_open()) {
		return false;
	}
	return input_pressed(action + (ship->player == 1 ? A_P2_UP : 0));
}

float netplay_analog_response(ship_t *ship) {
	if (ship_is_remote_player(ship)) {
		net_slot_t *slot = netplay_remote_slot(ship);
		if (slot && slot->input.analog_response > 0) {
			return slot->input.analog_response / 20.0f;
		}
	}
	return save.analog_response;
}


// -----------------------------------------------------------------------------
// Events

static net_event_t *netplay_event_push(net_event_type_t type) {
	if (!netplay_is_host() || net.phase != NET_PHASE_RACE) {
		return NULL;
	}
	np.event_seq++;
	int index = np.event_seq % len(np.events);
	np.events[index].time = net.now;
	net_event_t *e = &np.events[index].ev;
	memset(e, 0, sizeof(*e));
	e->seq = np.event_seq;
	e->type = type;
	e->target = -1;
	return e;
}

sfx_t *netplay_sfx_play_for(ship_t *ship, sfx_source_t source, float pitch) {
	if (ship_is_local_player(ship)) {
		sfx_t *sfx = sfx_play(source);
		if (pitch > 0) {
			sfx->pitch = pitch;
		}
		return sfx;
	}
	if (ship_is_remote_player(ship)) {
		net_event_t *e = netplay_event_push(NET_EVENT_SFX);
		if (e) {
			e->target = ship->pilot;
			e->a = source;
			e->value = pitch;
		}
	}
	return NULL;
}

void netplay_shake_for(ship_t *ship, float duration) {
	if (ship_is_local_player(ship)) {
		camera_set_shake(game_ship_camera(ship), duration);
	}
	else if (ship_is_remote_player(ship)) {
		net_event_t *e = netplay_event_push(NET_EVENT_SHAKE);
		if (e) {
			e->target = ship->pilot;
			e->value = duration;
		}
	}
}

void netplay_event_sfx_at(sfx_source_t source, vec3_t pos, float volume) {
	net_event_t *e = netplay_event_push(NET_EVENT_SFX_AT);
	if (e) {
		e->a = source;
		e->pos = pos;
		e->value = volume;
	}
}

void netplay_event_explosion(vec3_t pos, int particle_type, vec3_t base_velocity, vec3_t back) {
	net_event_t *e = netplay_event_push(NET_EVENT_EXPLOSION);
	if (e) {
		// A ship hit has debris velocity, a track hit has the offset of the flash
		bool track_hit = back.x != 0 || back.y != 0 || back.z != 0;
		e->a = particle_type;
		e->b = track_hit;
		e->pos = pos;
		e->vec = track_hit ? back : base_velocity;
	}
}

void netplay_event_sparks(ship_t *ship, vec3_t pos, vec3_t normal, float strength) {
	net_event_t *e = netplay_event_push(NET_EVENT_SPARKS);
	if (e) {
		e->b = ship->pilot;
		e->pos = pos;
		e->vec = normal;
		e->value = strength;
	}
}

void netplay_event_flash(vec3_t pos, int particle_type) {
	net_event_t *e = netplay_event_push(NET_EVENT_FLASH);
	if (e) {
		e->a = particle_type;
		e->pos = pos;
	}
}

void netplay_event_countdown(int count) {
	net_event_t *e = netplay_event_push(NET_EVENT_COUNTDOWN);
	if (e) {
		e->a = count;
	}
}

static bool netplay_particle_type_valid(int type) {
	return type >= PARTICLE_TYPE_FIRE && type <= PARTICLE_TYPE_GREENY;
}

static void netplay_apply_event(const net_event_t *e) {
	ship_t *own = &g.ships[g.pilot];
	switch (e->type) {
		case NET_EVENT_SFX:
			if ((e->target < 0 || e->target == own->pilot) && e->a <= SFX_VOICE_COUNT_GO) {
				sfx_t *sfx = sfx_play(e->a);
				if (e->value > 0) {
					sfx->pitch = e->value;
				}
			}
			break;
		case NET_EVENT_SFX_AT:
			if (e->a <= SFX_VOICE_COUNT_GO) {
				sfx_play_at(e->a, e->pos, vec3(0, 0, 0), clamp(e->value, 0.0f, 1.0f));
			}
			break;
		case NET_EVENT_EXPLOSION:
			if (netplay_particle_type_valid(e->a)) {
				vec3_t zero = vec3(0, 0, 0);
				weapons_explosion_fx(e->pos, e->a, e->b ? zero : e->vec, e->b ? e->vec : zero);
			}
			break;
		case NET_EVENT_SPARKS:
			if (e->b < NUM_PILOTS) {
				ship_spawn_impact_sparks(&g.ships[e->b], e->pos, e->vec, e->value);
			}
			break;
		case NET_EVENT_FLASH:
			if (netplay_particle_type_valid(e->a)) {
				race_add_flash_light(e->pos, e->a);
			}
			break;
		case NET_EVENT_SHAKE:
			// The short shakes (e-bolt) are for the cockpit view only
			if (e->target == own->pilot && (np.view_internal || e->value > CAMERA_SHAKE_SHORT + 0.01)) {
				camera_set_shake(&g.cameras[0], clamp(e->value, 0.0f, 2.0f));
			}
			break;
		case NET_EVENT_COUNTDOWN:
			switch (e->a) {
				case 3: sfx_play(SFX_VOICE_COUNT_3); break;
				case 2: sfx_play(SFX_VOICE_COUNT_2); scene_set_start_booms(1); break;
				case 1: sfx_play(SFX_VOICE_COUNT_1); scene_set_start_booms(2); break;
				case 0: sfx_play(SFX_VOICE_COUNT_GO); scene_set_start_booms(3); break;
			}
			break;
	}
}


// -----------------------------------------------------------------------------
// Race: shared

static void netplay_client_reset_tracking(void);

void netplay_race_init(void) {
	if (!netplay_active()) {
		return;
	}
	np.first_finish_time = 0;
	np.results_shown = false;
	np.finished_shown = false;
	np.last_snapshot_time = 0;
	np.have_events = false;
	np.view_internal = !settings.external_view;
	np.bot_fire_timer = 0;
	for (int i = 0; i < MAX_PLAYERS; i++) {
		g.finish_rank[i] = 0;
	}

	if (netplay_is_host()) {
		np.event_seq = 0;
		memset(np.events, 0, sizeof(np.events));
		for (int i = 1; i < NET_MAX_PLAYERS; i++) {
			if (net.slots[i].used) {
				droid_init(&np.remote_droids[i], &g.ships[net.slots[i].pilot]);
			}
		}
		np.race_running = false;
	}
	else {
		// Engine sounds: the host's ship update functions would reserve these,
		// but here they never run
		for (int i = 0; i < len(g.ships); i++) {
			ship_t *ship = &g.ships[i];
			if (ship_is_local_player(ship)) {
				ship->sfx_engine_thrust = sfx_reserve_loop(SFX_ENGINE_THRUST);
				ship->sfx_engine_intake = sfx_reserve_loop(SFX_ENGINE_INTAKE);
				ship->sfx_shield = sfx_reserve_loop(SFX_SHIELD);
				ship->sfx_turbulence = sfx_reserve_loop(SFX_TURBULENCE);
			}
			else {
				ship->sfx_engine_thrust = sfx_reserve_loop(SFX_ENGINE_REMOTE);
				ship->sfx_engine_intake = NULL;
				ship->sfx_shield = NULL;
				ship->sfx_turbulence = NULL;
				sfx_set_position(ship->sfx_engine_thrust, ship->position, ship->velocity, 0.1);
			}
		}
		for (int i = 0; i < NET_MAX_PLAYERS; i++) {
			memset(&np.remote_droids[i], 0, sizeof(droid_t));
		}
		netplay_client_reset_tracking();
	}

	net_session_client_set_ready(&net, net.race_id);
	np.loaded_race_id = net.race_id;
}

bool netplay_race_waiting(void) {
	if (!netplay_is_host()) {
		return false;
	}
	if (np.race_running) {
		return false;
	}
	if (net.phase == NET_PHASE_LOADING && !net_session_host_all_ready(&net)) {
		return true;
	}
	if (net.phase == NET_PHASE_LOADING) {
		net_session_host_set_phase(&net, NET_PHASE_RACE);
		printf("net: everybody is ready, go\n");
	}
	np.race_running = true;
	return false;
}

void netplay_race_draw_droids(void) {
	if (!netplay_active()) {
		return;
	}
	// The other players' droids, while they are rescuing someone
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		if (i == net.my_slot || !net.slots[i].used) {
			continue;
		}
		droid_t *droid = &np.remote_droids[i];
		if (droid->update_func == droid_update_rescue || (netplay_is_client() && droid->siren_started)) {
			droid_draw(droid);
		}
	}
}

void netplay_race_draw_hud(void) {
	if (!netplay_active()) {
		return;
	}

	bool waiting = netplay_is_host()
		? !np.race_running
		: (net.phase == NET_PHASE_LOADING || !net.have_snapshot);
	if (waiting) {
		int ready = 0, total = 0;
		for (int i = 0; i < NET_MAX_PLAYERS; i++) {
			if (net.slots[i].used && net.slots[i].connected) {
				total++;
				if (net.slots[i].ready_race_id == net.race_id || (i == net.my_slot)) {
					ready++;
				}
			}
		}
		char text[48];
		snprintf(text, sizeof(text), "WAITING FOR PLAYERS %d OF %d", ready, total);
		ui_draw_text_centered(text, ui_scaled_pos(UI_POS_MIDDLE | UI_POS_CENTER, vec2i(0, -40)), UI_SIZE_8, UI_COLOR_ACCENT);
	}

	// Who is who: a name tag over the other players' ships
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		if (i == net.my_slot || !net.slots[i].used) {
			continue;
		}
		ship_t *ship = &g.ships[net.slots[i].pilot];
		if (vec3_len(vec3_sub(ship->position, g.camera->position)) > 24000) {
			continue;
		}
		vec3_t projected = render_transform(vec3_add(ship->position, vec3(0, -300, 0)));
		if (projected.z >= 1 || fabsf(projected.x) > 1 || fabsf(projected.y) > 1) {
			continue; // only the ones we can see; 7 labels at the border would be a mess
		}
		hud_draw_player_marker(ship, net.slots[i].name);
	}

	if (
		netplay_is_client() && net.status == NET_STATUS_OK && net.phase == NET_PHASE_RACE &&
		net.have_snapshot && net.now - net.snapshot_recv_time > 0.5
	) {
		ui_draw_text_centered("CONNECTION PROBLEM", ui_scaled_pos(UI_POS_TOP | UI_POS_CENTER, vec2i(0, 40)), UI_SIZE_8, UI_COLOR_ACCENT);
	}
}


// -----------------------------------------------------------------------------
// Race: host

static void netplay_host_end_race(void) {
	net_result_t results[NET_MAX_SHIPS];
	int num = 0;
	for (int rank = 1; rank <= NUM_PILOTS && num < NET_MAX_SHIPS; rank++) {
		for (int i = 0; i < NUM_PILOTS; i++) {
			ship_t *ship = &g.ships[i];
			if (ship->position_rank != rank) {
				continue;
			}
			net_result_t *r = &results[num++];
			r->pilot = i;
			r->slot = net_session_slot_for_pilot(&net, i);
			r->finished = ship->lap >= NUM_LAPS || ship->max_lap >= NUM_LAPS;
			r->race_time = 0;
			r->best_lap = 0;
			for (int l = 0; l < NUM_LAPS; l++) {
				float t = g.lap_times[i][l];
				r->race_time += t;
				if (t > 0 && (r->best_lap == 0 || t < r->best_lap)) {
					r->best_lap = t;
				}
			}
			break;
		}
	}
	net_session_host_set_results(&net, results, num);
	net_session_host_set_phase(&net, NET_PHASE_RESULTS);
	np.races_done++;
	printf("net: race over\n");
	if (np.cmd_print_stats) {
		netplay_print_stats("race");
	}
	race_release_control();
	race_show_menu(netplay_results_menu_init());
	np.results_shown = true;
}

void netplay_player_finished(ship_t *ship) {
	if (np.first_finish_time == 0) {
		np.first_finish_time = net.now;
	}
	if (ship_is_local_player(ship)) {
		g.finish_rank[0] = ship->position_rank;
	}
	race_release_ship(ship);
}

void netplay_host_race_update_begin(void) {
	if (!netplay_is_host()) {
		return;
	}
	net_session_host_latch_presses(&net);

	// Players that dropped out: their ship continues on autopilot
	for (int i = 1; i < NET_MAX_PLAYERS; i++) {
		net_slot_t *slot = &net.slots[i];
		if (slot->used && !slot->connected) {
			ship_t *ship = &g.ships[slot->pilot];
			if (flags_is(ship->flags, SHIP_RACING)) {
				printf("net: %s dropped out, the ship goes on autopilot\n", slot->name);
				race_release_ship(ship);
			}
		}
	}
}

static void netplay_host_send_snapshot(void) {
	static net_snapshot_t snap;
	static uint8_t buf[NET_MAX_PACKET];

	snap.session = net.session;
	snap.race_id = net.race_id;
	snap.seq = ++net.snapshot_send_seq;
	snap.host_time = (float)(net.now - net.start_time);

	snap.num_ships = NUM_PILOTS;
	for (int i = 0; i < NUM_PILOTS; i++) {
		ship_t *ship = &g.ships[i];
		net_ship_state_t *s = &snap.ships[i];
		s->flags = ship->flags;
		s->section = ship->section ? ship->section - g.track.sections : 0;
		s->lap = ship->lap;
		s->rank = ship->position_rank;
		s->weapon_type = ship->weapon_type;
		s->weapon_target = ship->weapon_target ? ship->weapon_target->pilot : -1;
		s->brake_left = clamp(ship->brake_left, 0.0f, 255.0f);
		s->brake_right = clamp(ship->brake_right, 0.0f, 255.0f);
		s->position = ship->position;
		s->velocity = ship->velocity;
		s->angle = ship->angle;
		s->angular_velocity = ship->angular_velocity;
		s->speed = ship->speed;
		s->thrust_mag = ship->thrust_mag;
		s->lap_time = ship->lap_time;
		s->update_timer = ship->update_timer;
		s->turbo_timer = ship->turbo_timer;
		for (int l = 0; l < NET_NUM_LAPS; l++) {
			s->lap_times[l] = g.lap_times[i][l];
		}
	}

	snap.num_weapons = weapons_net_export(snap.weapons, NET_MAX_WEAPONS);

	snap.num_droids = 0;
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		if (!net.slots[i].used) {
			continue;
		}
		droid_t *droid = i == 0 ? &g.droids[0] : &np.remote_droids[i];
		net_droid_state_t *d = &snap.droids[snap.num_droids++];
		d->pilot = net.slots[i].pilot;
		d->tractor = droid->update_func == droid_update_rescue;
		d->position = droid->position;
		d->angle = droid->angle;
	}

	snap.num_pickups = min(g.track.pickups_len, NET_MAX_PICKUPS);
	memset(snap.pickups, 0, sizeof(snap.pickups));
	for (int i = 0; i < snap.num_pickups; i++) {
		if (flags_is(g.track.pickups[i].face->flags, FACE_PICKUP_ACTIVE)) {
			snap.pickups[i >> 3] |= 1 << (i & 7);
		}
	}

	// The events of the last few hundred ms; every snapshot repeats them, so
	// a lost packet doesn't lose an explosion
	snap.num_events = 0;
	uint32_t first = np.event_seq >= len(np.events) ? np.event_seq - len(np.events) + 1 : 1;
	for (uint32_t seq = first; seq <= np.event_seq && np.event_seq > 0; seq++) {
		int index = seq % len(np.events);
		if (np.events[index].ev.seq != seq || net.now - np.events[index].time > NETPLAY_EVENT_WINDOW) {
			continue;
		}
		if (snap.num_events == NET_MAX_EVENTS) {
			// Too many: keep the newest
			memmove(snap.events, snap.events + 1, sizeof(net_event_t) * (NET_MAX_EVENTS - 1));
			snap.num_events--;
		}
		snap.events[snap.num_events++] = np.events[index].ev;
	}

	int len = net_encode_snapshot(buf, sizeof(buf), &snap);
	if (len) {
		net_session_host_send_snapshot(&net, buf, len);
	}
	else {
		printf("net: snapshot too large\n");
	}
}

void netplay_host_race_update_end(void) {
	if (!netplay_is_host()) {
		return;
	}

	// The other players' rescue droids
	for (int i = 1; i < NET_MAX_PLAYERS; i++) {
		if (net.slots[i].used) {
			droid_update(&np.remote_droids[i], &g.ships[net.slots[i].pilot]);
		}
	}

	if (net.phase == NET_PHASE_RACE && net.now - np.last_snapshot_time >= (1.0 / NETPLAY_SNAPSHOT_RATE) - 0.002) {
		np.last_snapshot_time = net.now;
		netplay_host_send_snapshot();
	}

	// Race over when all humans are done, or the stragglers ran out of time
	if (net.phase == NET_PHASE_RACE && np.race_running) {
		bool anyone_racing = false;
		for (int i = 0; i < NET_MAX_PLAYERS; i++) {
			if (net.slots[i].used && net.slots[i].connected && flags_is(g.ships[net.slots[i].pilot].flags, SHIP_RACING)) {
				anyone_racing = true;
			}
		}
		bool grace_over = np.first_finish_time > 0 && net.now - np.first_finish_time > NETPLAY_FINISH_GRACE_TIME;
		if (!anyone_racing || grace_over) {
			netplay_host_end_race();
		}
	}
}


// -----------------------------------------------------------------------------
// Race: client

typedef struct {
	net_ship_state_t state;
	net_ship_state_t prev;
	double recv_time;
	float snap_dt;       // host time between prev and state
	vec3_t pos_rate;     // per second, from the last two snapshots
	vec3_t angle_rate;
	vec3_t pos_error;    // smoothed away over a few frames
	vec3_t angle_error;
	bool valid;
	bool has_prev;
} netplay_ship_track_t;

static netplay_ship_track_t tracks[NUM_PILOTS];
static net_snapshot_t client_snap;
static double client_snap_time;

static void netplay_client_reset_tracking(void) {
	memset(tracks, 0, sizeof(tracks));
	memset(&client_snap, 0, sizeof(client_snap));
	client_snap_time = 0;
}

static vec3_t netplay_angle_diff(vec3_t a, vec3_t b) {
	return vec3(wrap_angle(a.x - b.x), wrap_angle(a.y - b.y), wrap_angle(a.z - b.z));
}

static void netplay_track_predict(netplay_ship_track_t *t, double now, vec3_t *pos, vec3_t *angle) {
	float age = clamp(now - t->recv_time, 0.0, 0.1);
	*pos = vec3_add(t->state.position, vec3_mulf(t->pos_rate, age));
	*angle = vec3_add(t->state.angle, vec3_mulf(t->angle_rate, age));
}

static bool netplay_validate_snapshot(net_snapshot_t *snap) {
	if (snap->num_ships != NUM_PILOTS) {
		return false;
	}
	for (int i = 0; i < snap->num_ships; i++) {
		if (snap->ships[i].section >= g.track.section_count) {
			return false;
		}
		if (snap->ships[i].weapon_type >= WEAPON_TYPE_MAX) {
			snap->ships[i].weapon_type = WEAPON_TYPE_NONE;
		}
	}
	return true;
}

static void netplay_client_take_snapshot(void) {
	const uint8_t *data;
	int len = net_session_client_take_snapshot(&net, &data);
	if (!len) {
		return;
	}
	static net_snapshot_t snap;
	if (!net_decode_snapshot(data, len, &snap) || snap.session != net.session || !netplay_validate_snapshot(&snap)) {
		net.stats.malformed++;
		return;
	}

	double now = net.now;
	for (int i = 0; i < NUM_PILOTS; i++) {
		netplay_ship_track_t *t = &tracks[i];
		net_ship_state_t *s = &snap.ships[i];

		vec3_t old_pos, old_angle;
		bool had = t->valid;
		if (had) {
			netplay_track_predict(t, now, &old_pos, &old_angle);
			old_pos = vec3_add(old_pos, t->pos_error);
			old_angle = vec3_add(old_angle, t->angle_error);
		}

		float host_dt = snap.host_time - client_snap.host_time;
		if (had && host_dt > 0.001 && host_dt < 0.25) {
			t->pos_rate = vec3_mulf(vec3_sub(s->position, t->state.position), 1.0 / host_dt);
			t->angle_rate = vec3_mulf(netplay_angle_diff(s->angle, t->state.angle), 1.0 / host_dt);
		}
		else {
			t->pos_rate = vec3(0, 0, 0);
			t->angle_rate = vec3(0, 0, 0);
		}

		t->prev = t->state;
		t->has_prev = had;
		t->state = *s;
		t->recv_time = now;
		t->valid = true;

		// Keep what is on screen continuous: the difference between where we
		// showed the ship and where it really is fades out over a few frames.
		// Big jumps (rescue, respawn) are taken as they are.
		if (had) {
			vec3_t new_pos, new_angle;
			netplay_track_predict(t, now, &new_pos, &new_angle);
			t->pos_error = vec3_sub(old_pos, new_pos);
			t->angle_error = netplay_angle_diff(old_angle, new_angle);
			if (vec3_len(t->pos_error) > 2500 || fabsf(t->angle_error.y) > 0.6) {
				t->pos_error = vec3(0, 0, 0);
				t->angle_error = vec3(0, 0, 0);
			}
		}
		else {
			t->pos_error = vec3(0, 0, 0);
			t->angle_error = vec3(0, 0, 0);
		}
	}

	// Events we haven't seen yet
	for (int i = 0; i < snap.num_events; i++) {
		net_event_t *e = &snap.events[i];
		if (!np.have_events || net_seq_newer(e->seq, np.last_event_seq)) {
			netplay_apply_event(e);
			np.last_event_seq = e->seq;
			np.have_events = true;
		}
	}

	client_snap = snap;
	client_snap_time = now;
}

static void netplay_client_update_ship(int i, double now, float tick) {
	netplay_ship_track_t *t = &tracks[i];
	ship_t *ship = &g.ships[i];
	net_ship_state_t *s = &t->state;
	if (!t->valid) {
		return;
	}

	// Fade the error out: about 90% gone after 0.15 s
	float fade = expf(-tick * 15.0f);
	t->pos_error = vec3_mulf(t->pos_error, fade);
	t->angle_error = vec3_mulf(t->angle_error, fade);

	vec3_t pos, angle;
	netplay_track_predict(t, now, &pos, &angle);
	ship->position = vec3_add(pos, t->pos_error);
	ship->angle = vec3_wrap_angle(vec3_add(angle, t->angle_error));

	float age = clamp(now - t->recv_time, 0.0, 0.1);
	bool local = ship_is_local_player(ship);
	int flags = s->flags;
	if (local) {
		// The view mode belongs to this machine
		if (np.view_internal) {
			flags_add(flags, SHIP_VIEW_INTERNAL);
		}
		else {
			flags_rm(flags, SHIP_VIEW_INTERNAL);
		}
	}
	ship->flags = flags;
	ship->prev_section = ship->section;
	ship->section = &g.track.sections[s->section];
	ship->section_num = ship->section->num;
	ship->velocity = s->velocity;
	ship->angular_velocity = s->angular_velocity;
	ship->speed = s->speed;
	ship->thrust_mag = s->thrust_mag;
	ship->brake_left = s->brake_left;
	ship->brake_right = s->brake_right;
	ship->lap = s->lap;
	ship->position_rank = s->rank;
	ship->weapon_type = s->weapon_type;
	ship->weapon_target = s->weapon_target >= 0 ? &g.ships[s->weapon_target] : NULL;
	ship->lap_time = s->lap_time + (flags_is(flags, SHIP_RACING) && s->update_timer <= 0 ? age : 0);
	ship->update_timer = s->update_timer;
	ship->turbo_timer = s->turbo_timer;
	for (int l = 0; l < NUM_LAPS; l++) {
		g.lap_times[i][l] = s->lap_times[l];
	}

	ship_update_cosmetics(ship);

	// Sounds
	if (local) {
		if (ship->sfx_engine_thrust) {
			ship_player_update_sfx(ship);
		}
	}
	else if (ship->sfx_engine_thrust) {
		sfx_set_position(ship->sfx_engine_thrust, ship->position, ship->velocity, s->update_timer > 0 && flags_is(flags, SHIP_RACING) ? 0.1 : 0.5);
	}
}

static void netplay_client_update_camera(ship_t *ship) {
	camera_t *camera = &g.cameras[0];

	// Change view
	if (input_pressed(A_CHANGE_VIEW) && !race_menu_is_open() && flags_is(ship->flags, SHIP_RACING)) {
		np.view_internal = !np.view_internal;
		if (camera->update_func == camera_update_race_internal || camera->update_func == camera_update_race_external) {
			camera->update_func = np.view_internal ? camera_update_race_internal : camera_update_race_external;
		}
		if (np.view_internal) {
			flags_add(ship->flags, SHIP_VIEW_INTERNAL);
		}
		else {
			flags_rm(ship->flags, SHIP_VIEW_INTERNAL);
		}
	}

	// Finished: the cinematic camera, like in a local game
	if (flags_not(ship->flags, SHIP_RACING)) {
		if (ship->lap >= NUM_LAPS && !np.finished_shown) {
			np.finished_shown = true;
			g.finish_rank[0] = ship->position_rank;
			camera->update_func = camera_update_attract_random;
		}
	}

	// Rescue: the host's droid sets the flags, the camera follows here
	else if (flags_is(ship->flags, SHIP_IN_RESCUE)) {
		if (camera->update_func != camera_update_rescue) {
			camera->update_func = camera_update_rescue;
			camera->section = flags_is(ship->section->flags, SECTION_JUMP) ? ship->section->next : ship->section;
		}
	}
	else if (camera->update_func == camera_update_rescue) {
		camera->update_func = np.view_internal ? camera_update_race_internal : camera_update_race_external;
	}

	camera_update(camera, ship, &g.droids[0]);

	// The intro camera picks the view from the settings; remember it
	if (camera->update_func == camera_update_race_internal) {
		np.view_internal = true;
	}
	else if (camera->update_func == camera_update_race_external) {
		np.view_internal = false;
	}
}

static void netplay_client_update_pickups(void) {
	float pickup_cycle_time = 1.5 * system_cycle_time();
	int num = min(client_snap.num_pickups, g.track.pickups_len);
	for (int i = 0; i < num; i++) {
		track_face_t *face = g.track.pickups[i].face;
		bool active = client_snap.pickups[i >> 3] & (1 << (i & 7));
		if (active) {
			flags_add(face->flags, FACE_PICKUP_ACTIVE);
			track_face_set_color(face, rgba(
				sinf( pickup_cycle_time + i) * 127 + 128,
				cosf( pickup_cycle_time + i) * 127 + 128,
				sinf(-pickup_cycle_time - i) * 127 + 128,
				255
			));
		}
		else if (flags_is(face->flags, FACE_PICKUP_ACTIVE)) {
			flags_rm(face->flags, FACE_PICKUP_ACTIVE);
			track_face_set_color(face, rgba(255, 255, 255, 255));
		}
	}
}

static void netplay_client_update_droids(double now) {
	float age = clamp(now - client_snap_time, 0.0, 0.1);
	(void)age;
	for (int i = 0; i < client_snap.num_droids; i++) {
		net_droid_state_t *d = &client_snap.droids[i];
		int slot = net_session_slot_for_pilot(&net, d->pilot);
		if (slot < 0) {
			continue;
		}
		droid_t *droid = slot == net.my_slot ? &g.droids[0] : &np.remote_droids[slot];
		droid->position = d->position;
		droid->angle = d->angle;
		droid->siren_started = d->tractor; // used here as "show this droid"
		if (slot == net.my_slot && droid->sfx_tractor) {
			if (d->tractor) {
				flags_add(droid->sfx_tractor->flags, SFX_PLAY);
				sfx_set_position(droid->sfx_tractor, droid->position, vec3(0, 0, 0), 0.5);
			}
			else {
				flags_rm(droid->sfx_tractor->flags, SFX_PLAY);
			}
		}
	}
}

void netplay_client_race_update(void) {
	netplay_client_take_snapshot();
	double now = net.now;
	float tick = system_tick();
	ship_t *own = &g.ships[g.pilot];

	if (client_snap.race_id == net.race_id && net.have_snapshot) {
		for (int i = 0; i < NUM_PILOTS; i++) {
			netplay_client_update_ship(i, now, tick);
		}
		float age = clamp(now - client_snap_time, 0.0, 0.1);
		weapons_net_import(client_snap.weapons, client_snap.num_weapons, age);
		netplay_client_update_droids(now);
		netplay_client_update_pickups();
	}
	weapons_update_client();
	netplay_client_update_camera(own);
}


// -----------------------------------------------------------------------------
// Menus

static void netplay_button_continue(menu_t *menu, int data) {
	race_unpause();
}

static void netplay_button_leave_confirm(menu_t *menu, int data) {
	if (data) {
		netplay_leave(netplay_is_host() ? "YOU ENDED THE GAME" : NULL);
		game_set_scene(GAME_SCENE_MAIN_MENU);
	}
	else {
		menu_pop(menu);
	}
}

static void netplay_button_leave(menu_t *menu, int data) {
	if (netplay_is_host()) {
		menu_confirm(menu, "END THE GAME", "FOR EVERYBODY?", "YES", "NO", netplay_button_leave_confirm);
	}
	else {
		menu_confirm(menu, "LEAVE", "THE GAME?", "YES", "NO", netplay_button_leave_confirm);
	}
}

menu_t *netplay_pause_menu_init(void) {
	if (!np.menu) {
		np.menu = mem_bump(sizeof(menu_t));
	}
	sfx_play(SFX_MENU_SELECT);
	menu_reset(np.menu);
	menu_page_t *page = menu_push(np.menu, "LAN GAME", NULL);
	menu_page_add_button(page, 0, "CONTINUE", netplay_button_continue);
	menu_page_add_button(page, 0, netplay_is_host() ? "END GAME" : "LEAVE GAME", netplay_button_leave);
	return np.menu;
}

static void netplay_button_back_to_lobby(menu_t *menu, int data) {
	if (netplay_is_host()) {
		net_session_host_set_phase(&net, NET_PHASE_LOBBY);
		if (np.cmd_races && np.races_done >= np.cmd_races) {
			printf("net: %d races done, quitting\n", np.races_done);
			netplay_leave(NULL);
			system_exit();
			return;
		}
	}
	game_set_scene(GAME_SCENE_MAIN_MENU);
}

static void netplay_page_results_draw(menu_t *menu, int data) {
	menu_page_t *page = &menu->pages[menu->index];
	ui_pos_t anchor = UI_POS_MIDDLE | UI_POS_CENTER;
	vec2i_t pos = vec2i(-150, page->title_pos.y + 28);

	ui_draw_text("POS", ui_scaled_pos(anchor, pos), UI_SIZE_8, UI_COLOR_ACCENT);
	ui_draw_text("PILOT", ui_scaled_pos(anchor, vec2i(pos.x + 36, pos.y)), UI_SIZE_8, UI_COLOR_ACCENT);
	ui_draw_text("PLAYER", ui_scaled_pos(anchor, vec2i(pos.x + 180, pos.y)), UI_SIZE_8, UI_COLOR_ACCENT);
	ui_draw_text("TIME", ui_scaled_pos(anchor, vec2i(pos.x + 262, pos.y)), UI_SIZE_8, UI_COLOR_ACCENT);
	pos.y += 16;

	for (int i = 0; i < net.num_results; i++) {
		net_result_t *r = &net.results[i];
		bool mine = r->slot >= 0 && r->slot == net.my_slot;
		rgba_t color = mine ? UI_COLOR_ACCENT : UI_COLOR_DEFAULT;
		ui_draw_number(i + 1, ui_scaled_pos(anchor, pos), UI_SIZE_8, color);
		ui_draw_text(def.pilots[r->pilot].name, ui_scaled_pos(anchor, vec2i(pos.x + 36, pos.y)), UI_SIZE_8, color);
		const char *player = r->slot >= 0 ? net.slots[r->slot].name : "CPU";
		ui_draw_text(player, ui_scaled_pos(anchor, vec2i(pos.x + 180, pos.y)), UI_SIZE_8, color);
		if (r->finished) {
			ui_draw_time(r->race_time, ui_scaled_pos(anchor, vec2i(pos.x + 262, pos.y)), UI_SIZE_8, color);
		}
		else {
			ui_draw_text("-", ui_scaled_pos(anchor, vec2i(pos.x + 262, pos.y)), UI_SIZE_8, color);
		}
		pos.y += 12;
	}

	if (netplay_is_client()) {
		ui_draw_text_centered("WAITING FOR THE HOST", ui_scaled_pos(anchor, vec2i(0, pos.y + 12)), UI_SIZE_8, UI_COLOR_DEFAULT);
	}
}

menu_t *netplay_results_menu_init(void) {
	if (!np.menu) {
		np.menu = mem_bump(sizeof(menu_t));
	}
	sfx_play(SFX_MENU_SELECT);
	menu_reset(np.menu);
	menu_page_t *page = menu_push(np.menu, "RACE RESULTS", netplay_page_results_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_anchor = UI_POS_MIDDLE | UI_POS_CENTER;
	page->title_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_MIDDLE | UI_POS_CENTER;
	page->items_pos = vec2i(0, 100);
	if (netplay_is_host()) {
		menu_page_add_button(page, 0, "BACK TO THE LOBBY", netplay_button_back_to_lobby);
	}
	menu_page_add_button(page, 0, netplay_is_host() ? "END GAME" : "LEAVE GAME", netplay_button_leave);
	return np.menu;
}

void netplay_host_auto_continue(void) {
	// Soak tests: nobody presses a button on the results screen
	if (netplay_is_host() && np.cmd_autostart && net.phase == NET_PHASE_RESULTS && net.now - net.phase_time > 3.0) {
		netplay_button_back_to_lobby(NULL, 0);
	}
}
