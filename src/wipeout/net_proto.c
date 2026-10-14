#include <string.h>
#include <math.h>

#include "net_proto.h"

// Byte stream helpers -------------------------------------------------------

void net_writer_init(net_writer_t *w, uint8_t *data, int capacity) {
	w->data = data;
	w->capacity = capacity;
	w->len = 0;
	w->overflow = false;
}

static bool net_writer_reserve(net_writer_t *w, int len) {
	if (w->overflow || w->len + len > w->capacity) {
		w->overflow = true;
		return false;
	}
	return true;
}

void net_write_u8(net_writer_t *w, uint8_t v) {
	if (net_writer_reserve(w, 1)) {
		w->data[w->len++] = v;
	}
}

void net_write_u16(net_writer_t *w, uint16_t v) {
	if (net_writer_reserve(w, 2)) {
		w->data[w->len++] = v & 0xff;
		w->data[w->len++] = (v >> 8) & 0xff;
	}
}

void net_write_u32(net_writer_t *w, uint32_t v) {
	if (net_writer_reserve(w, 4)) {
		w->data[w->len++] = v & 0xff;
		w->data[w->len++] = (v >> 8) & 0xff;
		w->data[w->len++] = (v >> 16) & 0xff;
		w->data[w->len++] = (v >> 24) & 0xff;
	}
}

void net_write_f32(net_writer_t *w, float v) {
	uint32_t bits;
	memcpy(&bits, &v, 4);
	net_write_u32(w, bits);
}

void net_write_vec3(net_writer_t *w, vec3_t v) {
	net_write_f32(w, v.x);
	net_write_f32(w, v.y);
	net_write_f32(w, v.z);
}

void net_write_bytes(net_writer_t *w, const void *src, int len) {
	if (net_writer_reserve(w, len)) {
		memcpy(w->data + w->len, src, len);
		w->len += len;
	}
}

void net_write_header_version(net_writer_t *w, uint8_t version, net_msg_type_t type) {
	net_write_u16(w, NET_PROTOCOL_MAGIC);
	net_write_u8(w, version);
	net_write_u8(w, type);
}

void net_write_header(net_writer_t *w, net_msg_type_t type) {
	net_write_header_version(w, NET_PROTOCOL_VERSION, type);
}

static void net_write_u24(net_writer_t *w, uint32_t v) {
	net_write_u8(w, v & 0xff);
	net_write_u16(w, (v >> 8) & 0xffff);
}

void net_reader_init(net_reader_t *r, const uint8_t *data, int len) {
	r->data = data;
	r->len = len;
	r->pos = 0;
	r->error = false;
}

static bool net_reader_take(net_reader_t *r, int len) {
	if (r->error || len < 0 || r->pos + len > r->len) {
		r->error = true;
		return false;
	}
	return true;
}

uint8_t net_read_u8(net_reader_t *r) {
	if (!net_reader_take(r, 1)) {
		return 0;
	}
	return r->data[r->pos++];
}

uint16_t net_read_u16(net_reader_t *r) {
	if (!net_reader_take(r, 2)) {
		return 0;
	}
	uint16_t v = r->data[r->pos] | (r->data[r->pos + 1] << 8);
	r->pos += 2;
	return v;
}

uint32_t net_read_u32(net_reader_t *r) {
	if (!net_reader_take(r, 4)) {
		return 0;
	}
	uint32_t v =
		(uint32_t)r->data[r->pos] |
		((uint32_t)r->data[r->pos + 1] << 8) |
		((uint32_t)r->data[r->pos + 2] << 16) |
		((uint32_t)r->data[r->pos + 3] << 24);
	r->pos += 4;
	return v;
}

float net_read_f32(net_reader_t *r) {
	uint32_t bits = net_read_u32(r);
	float v;
	memcpy(&v, &bits, 4);
	if (!isfinite(v)) {
		r->error = true;
		return 0;
	}
	return v;
}

vec3_t net_read_vec3(net_reader_t *r) {
	vec3_t v;
	v.x = net_read_f32(r);
	v.y = net_read_f32(r);
	v.z = net_read_f32(r);
	return v;
}

void net_read_bytes(net_reader_t *r, void *dst, int len) {
	if (!net_reader_take(r, len)) {
		memset(dst, 0, len);
		return;
	}
	memcpy(dst, r->data + r->pos, len);
	r->pos += len;
}

net_msg_type_t net_read_header(net_reader_t *r) {
	uint16_t magic = net_read_u16(r);
	uint8_t version = net_read_u8(r);
	uint8_t type = net_read_u8(r);
	if (r->error || magic != NET_PROTOCOL_MAGIC || version != NET_PROTOCOL_VERSION) {
		r->error = true;
		return 0;
	}
	if (type == 0 || type >= NET_MSG_MAX) {
		r->error = true;
		return 0;
	}
	return type;
}

net_msg_type_t net_peek_type(const uint8_t *buf, int len) {
	net_reader_t r;
	net_reader_init(&r, buf, len);
	return net_read_header(&r);
}

int net_peek_version(const uint8_t *buf, int len) {
	if (len < 4 || (buf[0] | (buf[1] << 8)) != NET_PROTOCOL_MAGIC) {
		return -1;
	}
	return buf[2];
}

static uint32_t net_read_u24(net_reader_t *r) {
	uint32_t lo = net_read_u8(r);
	return lo | ((uint32_t)net_read_u16(r) << 8);
}


// Quantization --------------------------------------------------------------

static int32_t net_round(float v) {
	return (int32_t)(v < 0 ? v - 0.5f : v + 0.5f);
}

static int16_t net_pack_s16(float v, float scale) {
	float x = v * scale;
	if (!(x > -32767.0f)) { return -32767; } // also NaN
	if (x > 32767.0f) { return 32767; }
	return (int16_t)net_round(x);
}

static uint16_t net_pack_u16(float v, float scale) {
	float x = v * scale;
	if (!(x > 0.0f)) { return 0; }
	if (x > 65535.0f) { return 65535; }
	return (uint16_t)net_round(x);
}

// -PI..PI onto the full 16 bit circle; +PI and -PI are the same angle
static int16_t net_pack_angle(float a) {
	if (!isfinite(a)) {
		return 0;
	}
	a = fmodf(a, (float)(M_PI * 2.0));
	int32_t q = net_round(a * (float)(32768.0 / M_PI));
	return (int16_t)(uint16_t)(q & 0xffff);
}

static float net_unpack_angle(int16_t q) {
	return q * (float)(M_PI / 32768.0);
}

float net_quantize_angle(float a) {
	return net_unpack_angle(net_pack_angle(a));
}

float net_quantize(float v, float scale) {
	return net_pack_s16(v, scale) / scale;
}

float net_quantize_u(float v, float scale) {
	return net_pack_u16(v, scale) / scale;
}

static void net_write_s16(net_writer_t *w, float v, float scale) {
	net_write_u16(w, (uint16_t)net_pack_s16(v, scale));
}

static float net_read_s16(net_reader_t *r, float scale) {
	return (int16_t)net_read_u16(r) / scale;
}

static void net_write_uq16(net_writer_t *w, float v, float scale) {
	net_write_u16(w, net_pack_u16(v, scale));
}

static float net_read_uq16(net_reader_t *r, float scale) {
	return net_read_u16(r) / scale;
}

static void net_write_angles(net_writer_t *w, vec3_t a) {
	net_write_u16(w, (uint16_t)net_pack_angle(a.x));
	net_write_u16(w, (uint16_t)net_pack_angle(a.y));
	net_write_u16(w, (uint16_t)net_pack_angle(a.z));
}

static vec3_t net_read_angles(net_reader_t *r) {
	vec3_t a;
	a.x = net_unpack_angle((int16_t)net_read_u16(r));
	a.y = net_unpack_angle((int16_t)net_read_u16(r));
	a.z = net_unpack_angle((int16_t)net_read_u16(r));
	return a;
}

static void net_write_svec3(net_writer_t *w, vec3_t v, float scale) {
	net_write_s16(w, v.x, scale);
	net_write_s16(w, v.y, scale);
	net_write_s16(w, v.z, scale);
}

static vec3_t net_read_svec3(net_reader_t *r, float scale) {
	vec3_t v;
	v.x = net_read_s16(r, scale);
	v.y = net_read_s16(r, scale);
	v.z = net_read_s16(r, scale);
	return v;
}

// 0..1 in 8 bit
static void net_write_unorm8(net_writer_t *w, float v) {
	float x = v * 255.0f;
	net_write_u8(w, !(x > 0.0f) ? 0 : x > 255.0f ? 255 : (uint8_t)net_round(x));
}

static float net_read_unorm8(net_reader_t *r) {
	return net_read_u8(r) / 255.0f;
}

// A unit vector, 8 bit per axis
static void net_write_normal8(net_writer_t *w, vec3_t n) {
	float c[3] = {n.x, n.y, n.z};
	for (int i = 0; i < 3; i++) {
		float x = c[i] * 127.0f;
		net_write_u8(w, (uint8_t)(int8_t)(!(x > -127.0f) ? -127 : x > 127.0f ? 127 : net_round(x)));
	}
}

static vec3_t net_read_normal8(net_reader_t *r) {
	vec3_t n;
	n.x = (int8_t)net_read_u8(r) / 127.0f;
	n.y = (int8_t)net_read_u8(r) / 127.0f;
	n.z = (int8_t)net_read_u8(r) / 127.0f;
	return n;
}

// The decoder must have consumed the packet exactly
static bool net_reader_done(net_reader_t *r) {
	return !r->error && r->pos == r->len;
}

static void net_write_name(net_writer_t *w, const char *name) {
	char padded[NET_NAME_LEN];
	memset(padded, 0, sizeof(padded));
	for (int i = 0; name[i] && i < NET_NAME_LEN - 1; i++) {
		padded[i] = name[i];
	}
	net_write_bytes(w, padded, NET_NAME_LEN);
}

void net_sanitize_name(char dst[NET_NAME_LEN], const char *src) {
	char out[NET_NAME_LEN];
	int len = 0;
	bool pending_space = false;
	for (int i = 0; src && src[i] && len < NET_NAME_MAX_CHARS; i++) {
		char c = src[i];
		if (c >= 'a' && c <= 'z') {
			c = c - 'a' + 'A';
		}
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
			if (pending_space && len > 0 && len < NET_NAME_MAX_CHARS - 1) {
				out[len++] = ' ';
			}
			pending_space = false;
			out[len++] = c;
		}
		else {
			pending_space = true;
		}
	}
	out[len] = '\0';
	memset(dst, 0, NET_NAME_LEN);
	memcpy(dst, len ? out : "PLAYER", len ? len : 6);
}

static void net_read_name(net_reader_t *r, char *name) {
	char raw[NET_NAME_LEN + 1];
	net_read_bytes(r, raw, NET_NAME_LEN);
	raw[NET_NAME_LEN] = '\0';
	net_sanitize_name(name, raw);
}

static int net_writer_finish(net_writer_t *w) {
	return w->overflow ? 0 : w->len;
}


// Messages ------------------------------------------------------------------

int net_encode_discover(uint8_t *buf, int cap) {
	net_writer_t w;
	net_writer_init(&w, buf, cap);
	net_write_header(&w, NET_MSG_DISCOVER);
	return net_writer_finish(&w);
}

int net_encode_host_info(uint8_t *buf, int cap, const net_msg_host_info_t *m) {
	return net_encode_host_info_version(buf, cap, NET_PROTOCOL_VERSION, m);
}

int net_encode_host_info_version(uint8_t *buf, int cap, uint8_t version, const net_msg_host_info_t *m) {
	net_writer_t w;
	net_writer_init(&w, buf, cap);
	net_write_header_version(&w, version, NET_MSG_HOST_INFO);
	net_write_u32(&w, m->session);
	net_write_u8(&w, m->phase);
	net_write_u8(&w, m->num_players);
	net_write_u8(&w, m->max_players);
	net_write_name(&w, m->name);
	return net_writer_finish(&w);
}

bool net_decode_host_info(const uint8_t *buf, int len, net_msg_host_info_t *m) {
	net_reader_t r;
	net_reader_init(&r, buf, len);
	if (net_read_header(&r) != NET_MSG_HOST_INFO) {
		return false;
	}
	m->session = net_read_u32(&r);
	m->phase = net_read_u8(&r);
	m->num_players = net_read_u8(&r);
	m->max_players = net_read_u8(&r);
	net_read_name(&r, m->name);
	return net_reader_done(&r) &&
		m->phase < NET_PHASE_MAX &&
		m->num_players <= NET_MAX_PLAYERS &&
		m->max_players <= NET_MAX_PLAYERS;
}

int net_encode_join(uint8_t *buf, int cap, const net_msg_join_t *m) {
	net_writer_t w;
	net_writer_init(&w, buf, cap);
	net_write_header(&w, NET_MSG_JOIN);
	net_write_u32(&w, m->nonce);
	net_write_u8(&w, m->pref_pilot);
	net_write_name(&w, m->name);
	return net_writer_finish(&w);
}

bool net_decode_join(const uint8_t *buf, int len, net_msg_join_t *m) {
	net_reader_t r;
	net_reader_init(&r, buf, len);
	if (net_read_header(&r) != NET_MSG_JOIN) {
		return false;
	}
	m->nonce = net_read_u32(&r);
	m->pref_pilot = net_read_u8(&r);
	net_read_name(&r, m->name);
	return net_reader_done(&r) && m->nonce != 0 && m->pref_pilot < NET_MAX_SHIPS;
}

static void net_write_input(net_writer_t *w, const net_input_t *input) {
	net_write_bytes(w, input->analog, NET_NUM_ACTIONS);
	net_write_bytes(w, input->presses, NET_NUM_ACTIONS);
	net_write_u8(w, input->analog_response);
}

static void net_read_input(net_reader_t *r, net_input_t *input) {
	net_read_bytes(r, input->analog, NET_NUM_ACTIONS);
	net_read_bytes(r, input->presses, NET_NUM_ACTIONS);
	input->analog_response = net_read_u8(r);
}

int net_encode_client_state(uint8_t *buf, int cap, const net_msg_client_state_t *m) {
	net_writer_t w;
	net_writer_init(&w, buf, cap);
	net_write_header(&w, NET_MSG_CLIENT_STATE);
	net_write_u32(&w, m->session);
	net_write_u32(&w, m->nonce);
	net_write_u16(&w, m->ready_race_id);
	net_write_u32(&w, m->snapshot_ack);
	net_write_f32(&w, m->echo_time);
	net_write_u8(&w, m->pref_pilot);
	net_write_input(&w, &m->input);
	return net_writer_finish(&w);
}

bool net_decode_client_state(const uint8_t *buf, int len, net_msg_client_state_t *m) {
	net_reader_t r;
	net_reader_init(&r, buf, len);
	if (net_read_header(&r) != NET_MSG_CLIENT_STATE) {
		return false;
	}
	m->session = net_read_u32(&r);
	m->nonce = net_read_u32(&r);
	m->ready_race_id = net_read_u16(&r);
	m->snapshot_ack = net_read_u32(&r);
	m->echo_time = net_read_f32(&r);
	m->pref_pilot = net_read_u8(&r);
	net_read_input(&r, &m->input);
	return net_reader_done(&r) && m->nonce != 0 && m->pref_pilot < NET_MAX_SHIPS;
}

int net_encode_leave(uint8_t *buf, int cap, const net_msg_leave_t *m) {
	net_writer_t w;
	net_writer_init(&w, buf, cap);
	net_write_header(&w, NET_MSG_LEAVE);
	net_write_u32(&w, m->session);
	net_write_u32(&w, m->nonce);
	return net_writer_finish(&w);
}

bool net_decode_leave(const uint8_t *buf, int len, net_msg_leave_t *m) {
	net_reader_t r;
	net_reader_init(&r, buf, len);
	if (net_read_header(&r) != NET_MSG_LEAVE) {
		return false;
	}
	m->session = net_read_u32(&r);
	m->nonce = net_read_u32(&r);
	return net_reader_done(&r);
}

int net_encode_lobby(uint8_t *buf, int cap, const net_msg_lobby_t *m) {
	net_writer_t w;
	net_writer_init(&w, buf, cap);
	net_write_header(&w, NET_MSG_LOBBY);
	net_write_u32(&w, m->session);
	net_write_f32(&w, m->host_time);
	net_write_u8(&w, m->phase);
	net_write_u16(&w, m->race_id);
	net_write_u8(&w, m->race_class);
	net_write_u8(&w, m->circuit);
	net_write_u8(&w, (uint8_t)m->your_slot);
	net_write_u8(&w, m->pause);
	net_write_u8(&w, m->pause_mask);
	net_write_uq16(&w, m->pause_timer, 100);
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		const net_slot_info_t *s = &m->slots[i];
		net_write_u8(&w, s->used);
		net_write_u8(&w, s->connected);
		net_write_u8(&w, s->ready);
		net_write_u8(&w, s->pilot);
		net_write_u16(&w, s->ping_ms);
		net_write_u32(&w, s->nonce);
		net_write_name(&w, s->name);
	}
	net_write_u8(&w, m->num_results);
	for (int i = 0; i < m->num_results && i < NET_MAX_SHIPS; i++) {
		const net_result_t *res = &m->results[i];
		net_write_u8(&w, res->pilot);
		net_write_u8(&w, (uint8_t)res->slot);
		net_write_u8(&w, res->finished);
		net_write_f32(&w, res->race_time);
		net_write_f32(&w, res->best_lap);
	}
	return net_writer_finish(&w);
}

bool net_decode_lobby(const uint8_t *buf, int len, net_msg_lobby_t *m) {
	net_reader_t r;
	net_reader_init(&r, buf, len);
	if (net_read_header(&r) != NET_MSG_LOBBY) {
		return false;
	}
	memset(m, 0, sizeof(*m));
	m->session = net_read_u32(&r);
	m->host_time = net_read_f32(&r);
	m->phase = net_read_u8(&r);
	m->race_id = net_read_u16(&r);
	m->race_class = net_read_u8(&r);
	m->circuit = net_read_u8(&r);
	m->your_slot = (int8_t)net_read_u8(&r);
	m->pause = net_read_u8(&r);
	m->pause_mask = net_read_u8(&r);
	m->pause_timer = net_read_uq16(&r, 100);
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		net_slot_info_t *s = &m->slots[i];
		s->used = net_read_u8(&r);
		s->connected = net_read_u8(&r);
		s->ready = net_read_u8(&r);
		s->pilot = net_read_u8(&r);
		s->ping_ms = net_read_u16(&r);
		s->nonce = net_read_u32(&r);
		net_read_name(&r, s->name);
		if (s->pilot >= NET_MAX_SHIPS) {
			return false;
		}
	}
	m->num_results = net_read_u8(&r);
	if (m->num_results > NET_MAX_SHIPS) {
		return false;
	}
	for (int i = 0; i < m->num_results; i++) {
		net_result_t *res = &m->results[i];
		res->pilot = net_read_u8(&r);
		res->slot = (int8_t)net_read_u8(&r);
		res->finished = net_read_u8(&r);
		res->race_time = net_read_f32(&r);
		res->best_lap = net_read_f32(&r);
		if (res->pilot >= NET_MAX_SHIPS || res->slot < -1 || res->slot >= NET_MAX_PLAYERS) {
			return false;
		}
	}
	return net_reader_done(&r) &&
		m->phase < NET_PHASE_MAX &&
		m->pause < NET_PAUSE_MAX &&
		m->your_slot >= -1 && m->your_slot < NET_MAX_PLAYERS;
}

int net_encode_reject(uint8_t *buf, int cap, const net_msg_reject_t *m) {
	return net_encode_reject_version(buf, cap, NET_PROTOCOL_VERSION, m);
}

int net_encode_reject_version(uint8_t *buf, int cap, uint8_t version, const net_msg_reject_t *m) {
	net_writer_t w;
	net_writer_init(&w, buf, cap);
	net_write_header_version(&w, version, NET_MSG_REJECT);
	net_write_u8(&w, m->reason);
	return net_writer_finish(&w);
}

bool net_decode_reject(const uint8_t *buf, int len, net_msg_reject_t *m) {
	net_reader_t r;
	net_reader_init(&r, buf, len);
	if (net_read_header(&r) != NET_MSG_REJECT) {
		return false;
	}
	m->reason = net_read_u8(&r);
	return net_reader_done(&r) && m->reason > 0 && m->reason < NET_REJECT_MAX;
}


// Snapshot ------------------------------------------------------------------

#define NET_SHIP_VELOCITY_SCALE   2.0f   // steps of 0.5
#define NET_SHIP_SPEED_SCALE      1.0f
#define NET_SHIP_THRUST_SCALE     4.0f
#define NET_TIMER_MS_SCALE        1000.0f
#define NET_WEAPON_VELOCITY_SCALE 4.0f   // steps of 0.25
#define NET_WEAPON_TIMER_SCALE    100.0f
#define NET_EVENT_VEC_SCALE       1.0f

#define NET_WEAPON_HAS_VELOCITY 0x40
#define NET_DROID_TRACTOR       0x80

static bool net_vec3_is_zero(vec3_t v) {
	return v.x == 0 && v.y == 0 && v.z == 0;
}

int net_encode_snapshot(uint8_t *buf, int cap, const net_snapshot_t *m) {
	if (
		m->num_ships > NET_MAX_SHIPS || m->num_weapons > NET_MAX_WEAPONS ||
		m->num_droids > NET_MAX_PLAYERS || m->num_pickups > NET_MAX_PICKUPS ||
		m->num_events > NET_MAX_EVENTS ||
		m->lap_times_pilot < -1 || m->lap_times_pilot >= NET_MAX_SHIPS
	) {
		return 0;
	}

	net_writer_t w;
	net_writer_init(&w, buf, cap);
	net_write_header(&w, NET_MSG_SNAPSHOT);
	net_write_u32(&w, m->session);
	net_write_u16(&w, m->race_id);
	net_write_u32(&w, m->seq);
	net_write_f32(&w, m->host_time);

	net_write_u8(&w, m->num_ships);
	for (int i = 0; i < m->num_ships; i++) {
		const net_ship_state_t *s = &m->ships[i];
		net_write_u24(&w, s->flags);
		net_write_u16(&w, (uint16_t)s->section);
		net_write_u8(&w, (uint8_t)s->lap);
		net_write_u8(&w, s->rank);
		net_write_u8(&w, s->weapon_type);
		net_write_u8(&w, (uint8_t)s->weapon_target);
		net_write_u8(&w, s->brake_left);
		net_write_u8(&w, s->brake_right);
		net_write_vec3(&w, s->position);
		net_write_svec3(&w, s->velocity, NET_SHIP_VELOCITY_SCALE);
		net_write_angles(&w, s->angle);
		net_write_uq16(&w, s->speed, NET_SHIP_SPEED_SCALE);
		net_write_uq16(&w, s->thrust_mag, NET_SHIP_THRUST_SCALE);
		net_write_f32(&w, s->lap_time);
		net_write_s16(&w, s->update_timer, NET_TIMER_MS_SCALE);
		net_write_uq16(&w, s->turbo_timer, NET_TIMER_MS_SCALE);
	}

	net_write_u8(&w, (uint8_t)m->lap_times_pilot);
	if (m->lap_times_pilot >= 0) {
		for (int l = 0; l < NET_NUM_LAPS; l++) {
			net_write_f32(&w, m->lap_times[l]);
		}
	}

	net_write_u8(&w, m->num_weapons);
	for (int i = 0; i < m->num_weapons; i++) {
		const net_weapon_state_t *wp = &m->weapons[i];
		if (wp->kind == 0 || wp->kind >= NET_WEAPON_KIND_MAX || wp->owner >= NET_MAX_SHIPS) {
			return 0;
		}
		bool moving = !net_vec3_is_zero(wp->velocity);
		net_write_u8(&w, wp->kind | (wp->owner << 3) | (moving ? NET_WEAPON_HAS_VELOCITY : 0));
		net_write_vec3(&w, wp->position);
		net_write_angles(&w, wp->angle);
		if (moving) {
			net_write_svec3(&w, wp->velocity, NET_WEAPON_VELOCITY_SCALE);
		}
		net_write_uq16(&w, wp->timer, NET_WEAPON_TIMER_SCALE);
	}

	net_write_u8(&w, m->num_droids);
	for (int i = 0; i < m->num_droids; i++) {
		const net_droid_state_t *d = &m->droids[i];
		if (d->pilot >= NET_MAX_SHIPS) {
			return 0;
		}
		net_write_u8(&w, d->pilot | (d->tractor ? NET_DROID_TRACTOR : 0));
		net_write_vec3(&w, d->position);
		net_write_angles(&w, d->angle);
	}

	net_write_u16(&w, m->num_pickups);
	net_write_bytes(&w, m->pickups, (m->num_pickups + 7) / 8);

	net_write_u8(&w, m->num_events);
	uint32_t base = m->num_events ? m->events[0].seq : 0;
	if (m->num_events) {
		net_write_u32(&w, base);
	}
	uint32_t prev = base;
	for (int i = 0; i < m->num_events; i++) {
		const net_event_t *e = &m->events[i];
		uint32_t offset = e->seq - base;
		if (offset > 0xffff || (i > 0 && !net_seq_newer(e->seq, prev))) {
			return 0;
		}
		prev = e->seq;
		net_write_u16(&w, (uint16_t)offset);
		net_write_u8(&w, e->type);
		switch (e->type) {
			case NET_EVENT_SFX:
				net_write_u8(&w, (uint8_t)e->target);
				net_write_u8(&w, e->a);
				net_write_f32(&w, e->value);
				break;
			case NET_EVENT_SFX_AT:
				net_write_u8(&w, e->a);
				net_write_vec3(&w, e->pos);
				net_write_unorm8(&w, e->value);
				break;
			case NET_EVENT_EXPLOSION:
				net_write_u8(&w, e->a);
				net_write_u8(&w, e->b);
				net_write_vec3(&w, e->pos);
				net_write_svec3(&w, e->vec, NET_EVENT_VEC_SCALE);
				break;
			case NET_EVENT_SPARKS:
				net_write_u8(&w, e->b);
				net_write_vec3(&w, e->pos);
				net_write_normal8(&w, e->vec);
				net_write_unorm8(&w, e->value);
				break;
			case NET_EVENT_FLASH:
				net_write_u8(&w, e->a);
				net_write_vec3(&w, e->pos);
				break;
			case NET_EVENT_SHAKE:
				net_write_u8(&w, (uint8_t)e->target);
				net_write_uq16(&w, e->value, NET_TIMER_MS_SCALE);
				break;
			case NET_EVENT_COUNTDOWN:
				net_write_u8(&w, e->a);
				break;
			default:
				return 0;
		}
	}

	return net_writer_finish(&w);
}

bool net_decode_snapshot(const uint8_t *buf, int len, net_snapshot_t *m) {
	net_reader_t r;
	net_reader_init(&r, buf, len);
	if (net_read_header(&r) != NET_MSG_SNAPSHOT) {
		return false;
	}
	m->session = net_read_u32(&r);
	m->race_id = net_read_u16(&r);
	m->seq = net_read_u32(&r);
	m->host_time = net_read_f32(&r);

	m->num_ships = net_read_u8(&r);
	if (m->num_ships > NET_MAX_SHIPS) {
		return false;
	}
	for (int i = 0; i < m->num_ships; i++) {
		net_ship_state_t *s = &m->ships[i];
		s->flags = net_read_u24(&r);
		s->section = (int16_t)net_read_u16(&r);
		s->lap = (int8_t)net_read_u8(&r);
		s->rank = net_read_u8(&r);
		s->weapon_type = net_read_u8(&r);
		s->weapon_target = (int8_t)net_read_u8(&r);
		s->brake_left = net_read_u8(&r);
		s->brake_right = net_read_u8(&r);
		s->position = net_read_vec3(&r);
		s->velocity = net_read_svec3(&r, NET_SHIP_VELOCITY_SCALE);
		s->angle = net_read_angles(&r);
		s->speed = net_read_uq16(&r, NET_SHIP_SPEED_SCALE);
		s->thrust_mag = net_read_uq16(&r, NET_SHIP_THRUST_SCALE);
		s->lap_time = net_read_f32(&r);
		s->update_timer = net_read_s16(&r, NET_TIMER_MS_SCALE);
		s->turbo_timer = net_read_uq16(&r, NET_TIMER_MS_SCALE);
		if (
			s->section < 0 || s->rank > NET_MAX_SHIPS ||
			s->weapon_target < -1 || s->weapon_target >= NET_MAX_SHIPS
		) {
			return false;
		}
	}

	m->lap_times_pilot = (int8_t)net_read_u8(&r);
	if (m->lap_times_pilot < -1 || m->lap_times_pilot >= NET_MAX_SHIPS) {
		return false;
	}
	for (int l = 0; l < NET_NUM_LAPS; l++) {
		m->lap_times[l] = m->lap_times_pilot >= 0 ? net_read_f32(&r) : 0;
	}

	m->num_weapons = net_read_u8(&r);
	if (m->num_weapons > NET_MAX_WEAPONS) {
		return false;
	}
	for (int i = 0; i < m->num_weapons; i++) {
		net_weapon_state_t *wp = &m->weapons[i];
		uint8_t packed = net_read_u8(&r);
		wp->kind = packed & 0x07;
		wp->owner = (packed >> 3) & 0x07;
		wp->position = net_read_vec3(&r);
		wp->angle = net_read_angles(&r);
		wp->velocity = (packed & NET_WEAPON_HAS_VELOCITY)
			? net_read_svec3(&r, NET_WEAPON_VELOCITY_SCALE)
			: vec3(0, 0, 0);
		wp->timer = net_read_uq16(&r, NET_WEAPON_TIMER_SCALE);
		if ((packed & 0x80) || wp->kind == 0 || wp->kind >= NET_WEAPON_KIND_MAX) {
			return false;
		}
	}

	m->num_droids = net_read_u8(&r);
	if (m->num_droids > NET_MAX_PLAYERS) {
		return false;
	}
	for (int i = 0; i < m->num_droids; i++) {
		net_droid_state_t *d = &m->droids[i];
		uint8_t packed = net_read_u8(&r);
		d->pilot = packed & 0x7f;
		d->tractor = (packed & NET_DROID_TRACTOR) ? 1 : 0;
		d->position = net_read_vec3(&r);
		d->angle = net_read_angles(&r);
		if (d->pilot >= NET_MAX_SHIPS) {
			return false;
		}
	}

	m->num_pickups = net_read_u16(&r);
	if (m->num_pickups > NET_MAX_PICKUPS) {
		return false;
	}
	memset(m->pickups, 0, sizeof(m->pickups));
	net_read_bytes(&r, m->pickups, (m->num_pickups + 7) / 8);

	m->num_events = net_read_u8(&r);
	if (m->num_events > NET_MAX_EVENTS) {
		return false;
	}
	uint32_t base = m->num_events ? net_read_u32(&r) : 0;
	for (int i = 0; i < m->num_events; i++) {
		net_event_t *e = &m->events[i];
		memset(e, 0, sizeof(*e));
		e->target = -1;
		e->seq = base + net_read_u16(&r);
		e->type = net_read_u8(&r);
		switch (e->type) {
			case NET_EVENT_SFX:
				e->target = (int8_t)net_read_u8(&r);
				e->a = net_read_u8(&r);
				e->value = net_read_f32(&r);
				break;
			case NET_EVENT_SFX_AT:
				e->a = net_read_u8(&r);
				e->pos = net_read_vec3(&r);
				e->value = net_read_unorm8(&r);
				break;
			case NET_EVENT_EXPLOSION:
				e->a = net_read_u8(&r);
				e->b = net_read_u8(&r);
				e->pos = net_read_vec3(&r);
				e->vec = net_read_svec3(&r, NET_EVENT_VEC_SCALE);
				break;
			case NET_EVENT_SPARKS:
				e->b = net_read_u8(&r);
				e->pos = net_read_vec3(&r);
				e->vec = net_read_normal8(&r);
				e->value = net_read_unorm8(&r);
				break;
			case NET_EVENT_FLASH:
				e->a = net_read_u8(&r);
				e->pos = net_read_vec3(&r);
				break;
			case NET_EVENT_SHAKE:
				e->target = (int8_t)net_read_u8(&r);
				e->value = net_read_uq16(&r, NET_TIMER_MS_SCALE);
				break;
			case NET_EVENT_COUNTDOWN:
				e->a = net_read_u8(&r);
				break;
			default:
				return false;
		}
		if (e->target < -1 || e->target >= NET_MAX_SHIPS) {
			return false;
		}
		if (i > 0 && !net_seq_newer(e->seq, m->events[i - 1].seq)) {
			return false;
		}
	}

	return net_reader_done(&r);
}
