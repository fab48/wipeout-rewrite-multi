#ifndef NET_SESSION_H
#define NET_SESSION_H

#include "../net.h"
#include "net_proto.h"

// The LAN session: a host with up to 7 clients. Everything is polled from the
// main loop through net_session_update(), nothing blocks. The host is the
// authority: it runs the race and sends snapshots; clients send their input.
// Slot 0 is always the host itself.
//
// Robustness rules:
//  - Lobby state is sent as a complete, idempotent message several times per
//    second, so a lost packet is simply replaced by the next one.
//  - Button presses travel as wrapping counters, so a press is never lost.
//  - A slot that is silent for too long is dropped; a client that doesn't
//    find itself in the lobby anymore joins again.

#define NET_LOBBY_SEND_INTERVAL   0.1
#define NET_CLIENT_SEND_INTERVAL_LOBBY 0.1
#define NET_CLIENT_SEND_INTERVAL_RACE  (1.0/90.0)
#define NET_DISCOVER_INTERVAL     1.0
#define NET_TIMEOUT               8.0
#define NET_TIMEOUT_LOADING       45.0 // loading a track with the xBR upscale can take a while
#define NET_FOUND_HOST_EXPIRE     3.5
#define NET_MAX_FOUND_HOSTS       8

typedef enum {
	NET_ROLE_NONE,
	NET_ROLE_HOST,
	NET_ROLE_CLIENT,
	NET_ROLE_DISCOVER
} net_role_t;

typedef enum {
	NET_STATUS_OK,
	NET_STATUS_JOINING,
	NET_STATUS_REJECTED,
	NET_STATUS_HOST_LOST,
	NET_STATUS_HOST_LEFT,
	NET_STATUS_ERROR
} net_status_t;

typedef struct {
	bool used;
	bool connected;
	uint32_t nonce;
	net_addr_t addr;
	double last_recv;
	char name[NET_NAME_LEN];
	uint8_t pilot;
	uint8_t pref_pilot;
	uint16_t ready_race_id;
	uint16_t ping_ms;
	uint32_t snapshot_ack;

	net_input_t input;
	uint8_t presses_seen[NET_NUM_ACTIONS];
	bool pressed[NET_NUM_ACTIONS]; // edges for the current host frame
} net_slot_t;

typedef struct {
	net_addr_t addr;
	double last_seen;
	net_msg_host_info_t info;
} net_found_host_t;

typedef struct {
	uint32_t packets_sent;
	uint32_t packets_received;
	uint32_t bytes_sent;
	uint32_t bytes_received;
	uint32_t malformed;
	uint32_t snapshots;
	uint32_t snapshots_late;  // arrived after a newer one: dropped
	uint32_t snapshots_missed; // gaps in the sequence
	double snapshot_max_gap;  // longest time without a snapshot during a race
	uint32_t slots_dropped;
} net_stats_t;

typedef struct {
	float loss;      // 0..1 chance to drop an outgoing packet
	float duplicate; // 0..1 chance to send it twice
} net_sim_t;

typedef struct net_session_t {
	net_role_t role;
	net_status_t status;
	uint8_t reject_reason;
	net_socket_t sock;
	uint16_t port;
	double now;
	double start_time;
	uint32_t rng;

	// Shared session state; owned by the host, mirrored on the clients
	uint32_t session;
	net_phase_t phase;
	uint16_t race_id;
	uint8_t race_class;
	uint8_t circuit;
	net_slot_t slots[NET_MAX_PLAYERS];
	int num_results;
	net_result_t results[NET_MAX_SHIPS];
	double phase_time; // when the current phase started

	// Host
	double next_lobby_send;
	uint32_t snapshot_send_seq;
	bool lobby_dirty;

	// Client
	uint32_t nonce;
	int my_slot;
	char name[NET_NAME_LEN];
	uint8_t pref_pilot;
	net_addr_t host_addr;
	double last_host_recv;
	double next_send;
	uint16_t ready_race_id;
	net_input_t input;
	float echo_host_time;
	double echo_recv_time;
	uint8_t snapshot_buf[NET_MAX_PACKET];
	int snapshot_len;
	uint32_t snapshot_seq;
	bool snapshot_new;
	double snapshot_recv_time;
	bool have_snapshot;

	// Discovery
	double next_discover;
	net_found_host_t found[NET_MAX_FOUND_HOSTS];
	int found_len;

	net_stats_t stats;
	net_sim_t sim;
} net_session_t;

void net_session_init(net_session_t *s);

// Host: opens the port and takes slot 0
bool net_session_host(net_session_t *s, uint16_t port, const char *name, int pilot, int race_class, int circuit);

// Client: looks for hosts on the LAN (broadcast) until join is called
bool net_session_discover(net_session_t *s, uint16_t port);
bool net_session_join(net_session_t *s, net_addr_t host, const char *name, int pref_pilot);

// Sends a goodbye and closes the socket
void net_session_close(net_session_t *s);

// Pump: receive and handle all waiting packets, time outs, periodic sends
void net_session_update(net_session_t *s, double now);

// Host
void net_session_host_set_race(net_session_t *s, int race_class, int circuit);
void net_session_host_set_pilot(net_session_t *s, int pilot);
void net_session_host_start_loading(net_session_t *s);
void net_session_host_set_phase(net_session_t *s, net_phase_t phase);
void net_session_host_set_results(net_session_t *s, const net_result_t *results, int num);
bool net_session_host_all_ready(net_session_t *s);
void net_session_host_send_snapshot(net_session_t *s, const uint8_t *data, int len);
// Turns the press counters received since the last call into edges
void net_session_host_latch_presses(net_session_t *s);
int net_session_num_players(net_session_t *s);

// Client
void net_session_client_set_ready(net_session_t *s, uint16_t race_id);
void net_session_client_set_pilot(net_session_t *s, int pilot);
// Returns the latest unseen snapshot, or 0
int net_session_client_take_snapshot(net_session_t *s, const uint8_t **data);
bool net_session_client_in_lobby(net_session_t *s);

int net_session_slot_for_pilot(net_session_t *s, int pilot);

#endif
