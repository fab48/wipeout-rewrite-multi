#ifndef NETPLAY_H
#define NETPLAY_H

#include "../types.h"
#include "net_session.h"
#include "ship.h"
#include "sfx.h"
#include "menu.h"

// LAN game: up to 8 players on 8 machines, one of them hosts. The host runs
// the whole race (physics, AI, weapons, pickups, rescue droids) exactly as in
// a local game; remote players are ships whose input comes from the network.
// Clients don't simulate anything: they show the snapshots the host sends
// (extrapolated and smoothed), play the events (sounds, explosions, sparks,
// camera shakes) and send their input back.

// ship->player for a human on another machine: NETPLAY_REMOTE_PLAYER + slot.
// ship_is_player() is true for these ships, ship_is_local_player() is not.
#define NETPLAY_REMOTE_PLAYER 8

#define ship_is_local_player(SHIP) ((SHIP)->player >= 0 && (SHIP)->player < NETPLAY_REMOTE_PLAYER)
#define ship_is_remote_player(SHIP) ((SHIP)->player >= NETPLAY_REMOTE_PLAYER)

// Snapshots per second sent by the host
#define NETPLAY_SNAPSHOT_RATE 60.0

// After the first human crossed the line, the others have this long to finish
#define NETPLAY_FINISH_GRACE_TIME 45.0

extern net_session_t net;

bool netplay_active(void);
bool netplay_is_host(void);
bool netplay_is_client(void);

void netplay_init(int argc, char **argv);
void netplay_cleanup(void);

// Every frame, in all scenes: network I/O and following the host around
void netplay_update(void);

// Start / leave sessions (from the menus)
bool netplay_host_session(void);
bool netplay_discover(void);
bool netplay_join(net_addr_t addr);
void netplay_leave(const char *reason);
const char *netplay_status_text(void);
bool netplay_take_return_to_lan(void); // once after leaving a LAN game

// Lobby
void netplay_host_start_race(void);
void netplay_set_pilot(int pilot);

// Who controls which ship
int netplay_player_for_pilot(int pilot);
float netplay_input_state(ship_t *ship, int action);
bool netplay_input_pressed(ship_t *ship, int action);
float netplay_analog_response(ship_t *ship);

// Feedback for a player's ship: played/shaken here for the local player,
// sent as an event for a remote one, nothing for an AI ship.
sfx_t *netplay_sfx_play_for(ship_t *ship, sfx_source_t source, float pitch);
void netplay_shake_for(ship_t *ship, float duration);

// Host: things the clients must see or hear too
void netplay_event_sfx_at(sfx_source_t source, vec3_t pos, float volume);
void netplay_event_explosion(vec3_t pos, int particle_type, vec3_t base_velocity, vec3_t back);
void netplay_event_sparks(ship_t *ship, vec3_t pos, vec3_t normal, float strength);
void netplay_event_flash(vec3_t pos, int particle_type);
void netplay_event_countdown(int count);

// Race integration
void netplay_race_init(void);
bool netplay_race_waiting(void); // host: not everybody has loaded the track yet
void netplay_host_race_update_begin(void);
void netplay_host_race_update_end(void);
void netplay_client_race_update(void);
void netplay_race_draw_droids(void);
void netplay_race_draw_hud(void);
void netplay_player_finished(ship_t *ship);
menu_t *netplay_pause_menu_init(void);
menu_t *netplay_results_menu_init(void);
void netplay_host_auto_continue(void);

// Headless soak tests: drive the local ship with a simple autopilot
bool netplay_bot_enabled(void);

#endif
