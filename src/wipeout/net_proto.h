#ifndef NET_PROTO_H
#define NET_PROTO_H

#include "../types.h"

// Wire format of the LAN game. Everything is little endian, floats are sent
// as their IEEE bit pattern. Every decoder checks bounds and value ranges, so
// that a truncated, corrupted or hostile datagram is rejected instead of
// crashing the game. Nothing in here depends on the game state.

#define NET_PROTOCOL_MAGIC   0x4f57 // "WO"
#define NET_PROTOCOL_VERSION 1
#define NET_DEFAULT_PORT     47800

#define NET_MAX_PLAYERS      8
#define NET_MAX_SHIPS        8
#define NET_MAX_WEAPONS      64
#define NET_MAX_EVENTS       48
#define NET_MAX_PICKUPS      256
#define NET_NAME_LEN         16
#define NET_NUM_ACTIONS      9   // A_UP .. A_CHANGE_VIEW
#define NET_NUM_LAPS         3
#define NET_MAX_PACKET       8192

typedef enum {
	NET_MSG_DISCOVER = 1,  // client -> broadcast: is there a host?
	NET_MSG_HOST_INFO,     // host -> client: answer to DISCOVER
	NET_MSG_JOIN,          // client -> host: let me in (repeated until in the lobby)
	NET_MSG_CLIENT_STATE,  // client -> host: input, ready state, acks; doubles as keep alive
	NET_MSG_LEAVE,         // either way: I'm gone
	NET_MSG_LOBBY,         // host -> client: the complete lobby / session state
	NET_MSG_SNAPSHOT,      // host -> client: the race state
	NET_MSG_REJECT,        // host -> client: can't join
	NET_MSG_MAX
} net_msg_type_t;

typedef enum {
	NET_PHASE_LOBBY,
	NET_PHASE_LOADING,  // everybody loads the track; the host waits for all to be ready
	NET_PHASE_RACE,
	NET_PHASE_RESULTS,
	NET_PHASE_MAX
} net_phase_t;

typedef enum {
	NET_REJECT_FULL = 1,
	NET_REJECT_IN_RACE,
	NET_REJECT_VERSION,
	NET_REJECT_MAX
} net_reject_t;


// Byte stream helpers -------------------------------------------------------

typedef struct {
	uint8_t *data;
	int capacity;
	int len;
	bool overflow;
} net_writer_t;

typedef struct {
	const uint8_t *data;
	int len;
	int pos;
	bool error;
} net_reader_t;

void net_writer_init(net_writer_t *w, uint8_t *data, int capacity);
void net_write_u8(net_writer_t *w, uint8_t v);
void net_write_u16(net_writer_t *w, uint16_t v);
void net_write_u32(net_writer_t *w, uint32_t v);
void net_write_f32(net_writer_t *w, float v);
void net_write_vec3(net_writer_t *w, vec3_t v);
void net_write_bytes(net_writer_t *w, const void *src, int len);
void net_write_header(net_writer_t *w, net_msg_type_t type);

void net_reader_init(net_reader_t *r, const uint8_t *data, int len);
uint8_t net_read_u8(net_reader_t *r);
uint16_t net_read_u16(net_reader_t *r);
uint32_t net_read_u32(net_reader_t *r);
float net_read_f32(net_reader_t *r); // non finite values set the error flag
vec3_t net_read_vec3(net_reader_t *r);
void net_read_bytes(net_reader_t *r, void *dst, int len);
// Checks magic and version; returns the message type or 0
net_msg_type_t net_read_header(net_reader_t *r);


// Messages ------------------------------------------------------------------

typedef struct {
	uint8_t analog[NET_NUM_ACTIONS];  // input_state() * 255
	uint8_t presses[NET_NUM_ACTIONS]; // wrapping press counters; robust against packet loss
	uint8_t analog_response;          // save.analog_response * 20
} net_input_t;

typedef struct {
	uint32_t session;
	uint8_t phase;
	uint8_t num_players;
	uint8_t max_players;
	char name[NET_NAME_LEN];
} net_msg_host_info_t;

typedef struct {
	uint32_t nonce;      // random per client instance; identifies it to the host
	uint8_t pref_pilot;
	char name[NET_NAME_LEN];
} net_msg_join_t;

typedef struct {
	uint32_t session;
	uint32_t nonce;
	uint16_t ready_race_id;  // race the client has loaded
	uint32_t snapshot_ack;   // latest snapshot sequence received
	float echo_time;         // host time of that snapshot/lobby, for the ping
	uint8_t pref_pilot;
	net_input_t input;
} net_msg_client_state_t;

typedef struct {
	uint32_t session;
	uint32_t nonce;
} net_msg_leave_t;

typedef struct {
	uint8_t used;
	uint8_t connected;
	uint8_t ready;     // has loaded the current race
	uint8_t pilot;
	uint16_t ping_ms;
	uint32_t nonce;
	char name[NET_NAME_LEN];
} net_slot_info_t;

typedef struct {
	uint8_t pilot;
	int8_t slot;        // -1 for AI ships
	uint8_t finished;
	float race_time;
	float best_lap;
} net_result_t;

typedef struct {
	uint32_t session;
	float host_time;
	uint8_t phase;
	uint16_t race_id;
	uint8_t race_class;
	uint8_t circuit;
	int8_t your_slot;   // the slot of the receiving client, -1 if none
	net_slot_info_t slots[NET_MAX_PLAYERS];
	uint8_t num_results; // only in NET_PHASE_RESULTS
	net_result_t results[NET_MAX_SHIPS];
} net_msg_lobby_t;

typedef struct {
	uint8_t reason;
} net_msg_reject_t;


// Snapshot ------------------------------------------------------------------

typedef struct {
	uint32_t flags;
	int16_t section;
	int8_t lap;
	uint8_t rank;
	uint8_t weapon_type;
	int8_t weapon_target; // pilot index or -1
	uint8_t brake_left;   // 0..255 for 0..256
	uint8_t brake_right;
	vec3_t position;
	vec3_t velocity;
	vec3_t angle;
	vec3_t angular_velocity;
	float speed;
	float thrust_mag;
	float lap_time;
	float update_timer;
	float turbo_timer;
	float lap_times[NET_NUM_LAPS];
} net_ship_state_t;

typedef enum {
	NET_WEAPON_MINE = 1,
	NET_WEAPON_MISSILE,
	NET_WEAPON_ROCKET,
	NET_WEAPON_EBOLT,
	NET_WEAPON_SHIELD,
	NET_WEAPON_KIND_MAX
} net_weapon_kind_t;

typedef struct {
	uint8_t kind;
	uint8_t owner; // pilot index
	vec3_t position;
	vec3_t angle;
	vec3_t velocity;
	float timer;
} net_weapon_state_t;

typedef struct {
	uint8_t pilot;   // the ship this droid looks after
	uint8_t tractor; // tractor beam sound on
	vec3_t position;
	vec3_t angle;
} net_droid_state_t;

typedef enum {
	NET_EVENT_SFX = 1,       // a: sound; target pilot or -1 for everybody; non positional
	NET_EVENT_SFX_AT,        // a: sound; pos; value: volume
	NET_EVENT_EXPLOSION,     // a: particle type; pos; b=0: vec is the debris velocity, b=1: the flash offset
	NET_EVENT_SPARKS,        // pos; vec: normal; value: strength; b: pilot (for the drift)
	NET_EVENT_FLASH,         // a: particle type; pos
	NET_EVENT_SHAKE,         // target pilot; value: duration
	NET_EVENT_COUNTDOWN,     // a: 3, 2, 1 or 0 for go
	NET_EVENT_MAX
} net_event_type_t;

typedef struct {
	uint32_t seq;
	uint8_t type;
	int8_t target;
	uint8_t a;
	uint8_t b;
	vec3_t pos;
	vec3_t vec;
	float value;
} net_event_t;

typedef struct {
	uint32_t session;
	uint16_t race_id;
	uint32_t seq;
	float host_time;
	uint8_t num_ships;
	net_ship_state_t ships[NET_MAX_SHIPS];
	uint8_t num_weapons;
	net_weapon_state_t weapons[NET_MAX_WEAPONS];
	uint8_t num_droids;
	net_droid_state_t droids[NET_MAX_PLAYERS];
	uint16_t num_pickups;
	uint8_t pickups[NET_MAX_PICKUPS / 8]; // active bits
	uint8_t num_events;
	net_event_t events[NET_MAX_EVENTS];
} net_snapshot_t;


// Encoders return the packet length, or 0 if it didn't fit. Decoders take a
// complete packet (header included) and return false if it is malformed.

int net_encode_discover(uint8_t *buf, int cap);
int net_encode_host_info(uint8_t *buf, int cap, const net_msg_host_info_t *m);
int net_encode_join(uint8_t *buf, int cap, const net_msg_join_t *m);
int net_encode_client_state(uint8_t *buf, int cap, const net_msg_client_state_t *m);
int net_encode_leave(uint8_t *buf, int cap, const net_msg_leave_t *m);
int net_encode_lobby(uint8_t *buf, int cap, const net_msg_lobby_t *m);
int net_encode_reject(uint8_t *buf, int cap, const net_msg_reject_t *m);
int net_encode_snapshot(uint8_t *buf, int cap, const net_snapshot_t *m);

net_msg_type_t net_peek_type(const uint8_t *buf, int len);
bool net_decode_host_info(const uint8_t *buf, int len, net_msg_host_info_t *m);
bool net_decode_join(const uint8_t *buf, int len, net_msg_join_t *m);
bool net_decode_client_state(const uint8_t *buf, int len, net_msg_client_state_t *m);
bool net_decode_leave(const uint8_t *buf, int len, net_msg_leave_t *m);
bool net_decode_lobby(const uint8_t *buf, int len, net_msg_lobby_t *m);
bool net_decode_reject(const uint8_t *buf, int len, net_msg_reject_t *m);
bool net_decode_snapshot(const uint8_t *buf, int len, net_snapshot_t *m);

// Sequence number comparison with wrap around
static inline bool net_seq_newer(uint32_t a, uint32_t b) {
	return (int32_t)(a - b) > 0;
}
static inline bool net_seq16_newer(uint16_t a, uint16_t b) {
	return (int16_t)(a - b) > 0;
}

#endif
