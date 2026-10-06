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

void net_write_header(net_writer_t *w, net_msg_type_t type) {
	net_write_u16(w, NET_PROTOCOL_MAGIC);
	net_write_u8(w, NET_PROTOCOL_VERSION);
	net_write_u8(w, type);
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

static void net_read_name(net_reader_t *r, char *name) {
	net_read_bytes(r, name, NET_NAME_LEN);
	name[NET_NAME_LEN - 1] = '\0';

	// Only what the UI font can draw
	for (int i = 0; name[i]; i++) {
		char c = name[i];
		if (c >= 'a' && c <= 'z') {
			name[i] = c - 'a' + 'A';
		}
		else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '.')) {
			name[i] = '-';
		}
	}
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
	net_writer_t w;
	net_writer_init(&w, buf, cap);
	net_write_header(&w, NET_MSG_HOST_INFO);
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
		m->your_slot >= -1 && m->your_slot < NET_MAX_PLAYERS;
}

int net_encode_reject(uint8_t *buf, int cap, const net_msg_reject_t *m) {
	net_writer_t w;
	net_writer_init(&w, buf, cap);
	net_write_header(&w, NET_MSG_REJECT);
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

int net_encode_snapshot(uint8_t *buf, int cap, const net_snapshot_t *m) {
	net_writer_t w;
	net_writer_init(&w, buf, cap);
	net_write_header(&w, NET_MSG_SNAPSHOT);
	net_write_u32(&w, m->session);
	net_write_u16(&w, m->race_id);
	net_write_u32(&w, m->seq);
	net_write_f32(&w, m->host_time);

	net_write_u8(&w, m->num_ships);
	for (int i = 0; i < m->num_ships && i < NET_MAX_SHIPS; i++) {
		const net_ship_state_t *s = &m->ships[i];
		net_write_u32(&w, s->flags);
		net_write_u16(&w, (uint16_t)s->section);
		net_write_u8(&w, (uint8_t)s->lap);
		net_write_u8(&w, s->rank);
		net_write_u8(&w, s->weapon_type);
		net_write_u8(&w, (uint8_t)s->weapon_target);
		net_write_u8(&w, s->brake_left);
		net_write_u8(&w, s->brake_right);
		net_write_vec3(&w, s->position);
		net_write_vec3(&w, s->velocity);
		net_write_vec3(&w, s->angle);
		net_write_vec3(&w, s->angular_velocity);
		net_write_f32(&w, s->speed);
		net_write_f32(&w, s->thrust_mag);
		net_write_f32(&w, s->lap_time);
		net_write_f32(&w, s->update_timer);
		net_write_f32(&w, s->turbo_timer);
		for (int l = 0; l < NET_NUM_LAPS; l++) {
			net_write_f32(&w, s->lap_times[l]);
		}
	}

	net_write_u8(&w, m->num_weapons);
	for (int i = 0; i < m->num_weapons && i < NET_MAX_WEAPONS; i++) {
		const net_weapon_state_t *wp = &m->weapons[i];
		net_write_u8(&w, wp->kind);
		net_write_u8(&w, wp->owner);
		net_write_vec3(&w, wp->position);
		net_write_vec3(&w, wp->angle);
		net_write_vec3(&w, wp->velocity);
		net_write_f32(&w, wp->timer);
	}

	net_write_u8(&w, m->num_droids);
	for (int i = 0; i < m->num_droids && i < NET_MAX_PLAYERS; i++) {
		const net_droid_state_t *d = &m->droids[i];
		net_write_u8(&w, d->pilot);
		net_write_u8(&w, d->tractor);
		net_write_vec3(&w, d->position);
		net_write_vec3(&w, d->angle);
	}

	net_write_u16(&w, m->num_pickups);
	int pickup_bytes = (m->num_pickups + 7) / 8;
	net_write_bytes(&w, m->pickups, pickup_bytes <= (int)sizeof(m->pickups) ? pickup_bytes : 0);

	net_write_u8(&w, m->num_events);
	for (int i = 0; i < m->num_events && i < NET_MAX_EVENTS; i++) {
		const net_event_t *e = &m->events[i];
		net_write_u32(&w, e->seq);
		net_write_u8(&w, e->type);
		net_write_u8(&w, (uint8_t)e->target);
		net_write_u8(&w, e->a);
		net_write_u8(&w, e->b);
		net_write_vec3(&w, e->pos);
		net_write_vec3(&w, e->vec);
		net_write_f32(&w, e->value);
	}

	if (
		m->num_ships > NET_MAX_SHIPS || m->num_weapons > NET_MAX_WEAPONS ||
		m->num_droids > NET_MAX_PLAYERS || m->num_pickups > NET_MAX_PICKUPS ||
		m->num_events > NET_MAX_EVENTS
	) {
		return 0;
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
		s->flags = net_read_u32(&r);
		s->section = (int16_t)net_read_u16(&r);
		s->lap = (int8_t)net_read_u8(&r);
		s->rank = net_read_u8(&r);
		s->weapon_type = net_read_u8(&r);
		s->weapon_target = (int8_t)net_read_u8(&r);
		s->brake_left = net_read_u8(&r);
		s->brake_right = net_read_u8(&r);
		s->position = net_read_vec3(&r);
		s->velocity = net_read_vec3(&r);
		s->angle = net_read_vec3(&r);
		s->angular_velocity = net_read_vec3(&r);
		s->speed = net_read_f32(&r);
		s->thrust_mag = net_read_f32(&r);
		s->lap_time = net_read_f32(&r);
		s->update_timer = net_read_f32(&r);
		s->turbo_timer = net_read_f32(&r);
		for (int l = 0; l < NET_NUM_LAPS; l++) {
			s->lap_times[l] = net_read_f32(&r);
		}
		if (
			s->section < 0 || s->rank > NET_MAX_SHIPS ||
			s->weapon_target < -1 || s->weapon_target >= NET_MAX_SHIPS
		) {
			return false;
		}
	}

	m->num_weapons = net_read_u8(&r);
	if (m->num_weapons > NET_MAX_WEAPONS) {
		return false;
	}
	for (int i = 0; i < m->num_weapons; i++) {
		net_weapon_state_t *wp = &m->weapons[i];
		wp->kind = net_read_u8(&r);
		wp->owner = net_read_u8(&r);
		wp->position = net_read_vec3(&r);
		wp->angle = net_read_vec3(&r);
		wp->velocity = net_read_vec3(&r);
		wp->timer = net_read_f32(&r);
		if (wp->kind == 0 || wp->kind >= NET_WEAPON_KIND_MAX || wp->owner >= NET_MAX_SHIPS) {
			return false;
		}
	}

	m->num_droids = net_read_u8(&r);
	if (m->num_droids > NET_MAX_PLAYERS) {
		return false;
	}
	for (int i = 0; i < m->num_droids; i++) {
		net_droid_state_t *d = &m->droids[i];
		d->pilot = net_read_u8(&r);
		d->tractor = net_read_u8(&r);
		d->position = net_read_vec3(&r);
		d->angle = net_read_vec3(&r);
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
	for (int i = 0; i < m->num_events; i++) {
		net_event_t *e = &m->events[i];
		e->seq = net_read_u32(&r);
		e->type = net_read_u8(&r);
		e->target = (int8_t)net_read_u8(&r);
		e->a = net_read_u8(&r);
		e->b = net_read_u8(&r);
		e->pos = net_read_vec3(&r);
		e->vec = net_read_vec3(&r);
		e->value = net_read_f32(&r);
		if (e->type == 0 || e->type >= NET_EVENT_MAX || e->target < -1 || e->target >= NET_MAX_SHIPS) {
			return false;
		}
	}

	return net_reader_done(&r);
}
