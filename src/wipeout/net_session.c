#include <string.h>

#include "net_session.h"

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
	#include <unistd.h>
#elif defined(_WIN32)
	#include <process.h>
#endif

static uint32_t net_rng_next(net_session_t *s) {
	// xorshift32
	uint32_t x = s->rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	s->rng = x;
	return x;
}

static float net_rng_float(net_session_t *s) {
	return (net_rng_next(s) & 0xffffff) / (float)0x1000000;
}

static uint32_t net_random_id(net_session_t *s) {
	uint32_t id;
	do {
		id = net_rng_next(s);
	} while (id == 0);
	return id;
}

void net_session_init(net_session_t *s) {
	net_sim_t sim = s->sim; // keep a configured loss simulation
	memset(s, 0, sizeof(*s));
	s->sim = sim;
	s->my_slot = -1;

	// Seed from the clock, the process and the stack, so that instances
	// started at the same moment on one machine still differ
	double t = net_time();
	uint64_t bits;
	memcpy(&bits, &t, sizeof(bits));
	uint32_t pid = 0;
	#if defined(_WIN32)
		pid = (uint32_t)_getpid();
	#elif !defined(__EMSCRIPTEN__)
		pid = (uint32_t)getpid();
	#endif
	s->rng = (uint32_t)(bits ^ (bits >> 32)) ^ (pid * 2654435761u) ^ (uint32_t)(uintptr_t)s;
	if (s->rng == 0) {
		s->rng = 0x12345678;
	}
	for (int i = 0; i < 8; i++) {
		net_rng_next(s);
	}
}

static void net_copy_name(char *dst, const char *src) {
	memset(dst, 0, NET_NAME_LEN);
	for (int i = 0; src && src[i] && i < NET_NAME_LEN - 1; i++) {
		dst[i] = src[i];
	}
}

static void net_send(net_session_t *s, net_addr_t to, const uint8_t *data, int len) {
	if (len <= 0) {
		return;
	}
	if (s->sim.loss > 0 && net_rng_float(s) < s->sim.loss) {
		return;
	}
	int copies = (s->sim.duplicate > 0 && net_rng_float(s) < s->sim.duplicate) ? 2 : 1;
	for (int i = 0; i < copies; i++) {
		if (net_socket_send(&s->sock, to, data, len)) {
			s->stats.packets_sent++;
			s->stats.bytes_sent += len;
		}
	}
}

int net_session_slot_for_pilot(net_session_t *s, int pilot) {
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		if (s->slots[i].used && s->slots[i].pilot == pilot) {
			return i;
		}
	}
	return -1;
}

int net_session_num_players(net_session_t *s) {
	int num = 0;
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		if (s->slots[i].used) {
			num++;
		}
	}
	return num;
}

static bool net_pilot_taken(net_session_t *s, int pilot, int except_slot) {
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		if (i != except_slot && s->slots[i].used && s->slots[i].pilot == pilot) {
			return true;
		}
	}
	return false;
}

// The preferred pilot if it's free, otherwise the next free one
static int net_assign_pilot(net_session_t *s, int slot, int pref) {
	for (int i = 0; i < NET_MAX_SHIPS; i++) {
		int pilot = (pref + i) % NET_MAX_SHIPS;
		if (!net_pilot_taken(s, pilot, slot)) {
			return pilot;
		}
	}
	return pref; // can't happen: there are as many pilots as slots
}


// Host ----------------------------------------------------------------------

bool net_session_host(net_session_t *s, uint16_t port, const char *name, int pilot, int race_class, int circuit) {
	net_session_init(s);
	if (!net_socket_open(&s->sock, port, true)) {
		s->status = NET_STATUS_ERROR;
		return false;
	}
	s->role = NET_ROLE_HOST;
	s->status = NET_STATUS_OK;
	s->port = port;
	s->session = net_random_id(s);
	s->phase = NET_PHASE_LOBBY;
	s->race_class = race_class;
	s->circuit = circuit;
	s->race_id = net_rng_next(s) & 0x7fff;
	s->start_time = s->now = net_time();
	s->phase_time = s->now;

	net_slot_t *host = &s->slots[0];
	host->used = true;
	host->connected = true;
	host->nonce = net_random_id(s);
	host->pilot = host->pref_pilot = pilot % NET_MAX_SHIPS;
	net_copy_name(host->name, name);
	s->my_slot = 0;
	s->nonce = host->nonce;
	net_copy_name(s->name, name);
	s->lobby_dirty = true;
	return true;
}

static float net_session_clock(net_session_t *s) {
	return (float)(s->now - s->start_time);
}

// your_slot -1 tells a client that it is not (or no longer) in the game
static void net_host_send_lobby_to(net_session_t *s, net_addr_t to, int slot_index) {
	net_msg_lobby_t m;
	memset(&m, 0, sizeof(m));
	m.session = s->session;
	m.host_time = net_session_clock(s);
	m.phase = s->phase;
	m.race_id = s->race_id;
	m.race_class = s->race_class;
	m.circuit = s->circuit;
	m.your_slot = slot_index;
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		net_slot_t *src = &s->slots[i];
		net_slot_info_t *dst = &m.slots[i];
		dst->used = src->used;
		dst->connected = src->connected;
		dst->ready = src->used && (i == 0 ? s->ready_race_id == s->race_id : src->ready_race_id == s->race_id);
		dst->pilot = src->pilot;
		dst->ping_ms = src->ping_ms;
		dst->nonce = src->nonce;
		memcpy(dst->name, src->name, NET_NAME_LEN);
	}
	if (s->phase == NET_PHASE_RESULTS) {
		m.num_results = s->num_results;
		memcpy(m.results, s->results, sizeof(m.results));
	}

	uint8_t buf[NET_MAX_PACKET];
	int len = net_encode_lobby(buf, sizeof(buf), &m);
	net_send(s, to, buf, len);
}

static void net_host_send_lobby(net_session_t *s) {
	for (int i = 1; i < NET_MAX_PLAYERS; i++) {
		if (s->slots[i].used && s->slots[i].connected) {
			net_host_send_lobby_to(s, s->slots[i].addr, i);
		}
	}
	s->next_lobby_send = s->now + NET_LOBBY_SEND_INTERVAL;
	s->lobby_dirty = false;
}

static void net_host_reject(net_session_t *s, net_addr_t to, net_reject_t reason) {
	uint8_t buf[64];
	net_msg_reject_t m = {.reason = reason};
	net_send(s, to, buf, net_encode_reject(buf, sizeof(buf), &m));
}

static int net_host_find_slot(net_session_t *s, uint32_t nonce) {
	for (int i = 1; i < NET_MAX_PLAYERS; i++) {
		if (s->slots[i].used && s->slots[i].nonce == nonce) {
			return i;
		}
	}
	return -1;
}

static void net_host_handle_join(net_session_t *s, net_addr_t from, const net_msg_join_t *m) {
	int index = net_host_find_slot(s, m->nonce);
	if (index >= 0) {
		// Already in; maybe from a new port
		net_slot_t *slot = &s->slots[index];
		slot->addr = from;
		slot->last_recv = s->now;
		if (!slot->connected) {
			slot->connected = true;
		}
		s->lobby_dirty = true;
		return;
	}

	if (s->phase != NET_PHASE_LOBBY) {
		net_host_reject(s, from, NET_REJECT_IN_RACE);
		return;
	}

	for (int i = 1; i < NET_MAX_PLAYERS; i++) {
		net_slot_t *slot = &s->slots[i];
		if (slot->used) {
			continue;
		}
		memset(slot, 0, sizeof(*slot));
		slot->used = true;
		slot->connected = true;
		slot->nonce = m->nonce;
		slot->addr = from;
		slot->last_recv = s->now;
		slot->pref_pilot = m->pref_pilot;
		slot->pilot = net_assign_pilot(s, i, m->pref_pilot);
		slot->ready_race_id = s->race_id - 1;
		net_copy_name(slot->name, m->name);
		s->lobby_dirty = true;
		return;
	}

	net_host_reject(s, from, NET_REJECT_FULL);
}

static void net_host_handle_client_state(net_session_t *s, net_addr_t from, const net_msg_client_state_t *m) {
	if (m->session != s->session) {
		return;
	}
	int index = net_host_find_slot(s, m->nonce);
	if (index < 0) {
		// Dropped (time out): tell it, so that it joins again
		net_host_send_lobby_to(s, from, -1);
		return;
	}
	net_slot_t *slot = &s->slots[index];
	slot->addr = from;
	slot->last_recv = s->now;
	if (!slot->connected) {
		slot->connected = true;
		s->lobby_dirty = true;
	}
	if (slot->ready_race_id != m->ready_race_id) {
		slot->ready_race_id = m->ready_race_id;
		s->lobby_dirty = true;
	}
	if (net_seq_newer(m->snapshot_ack, slot->snapshot_ack)) {
		slot->snapshot_ack = m->snapshot_ack;
	}

	float rtt = net_session_clock(s) - m->echo_time;
	if (m->echo_time > 0 && rtt >= 0 && rtt < 10) {
		slot->ping_ms = (uint16_t)(rtt * 1000);
	}

	if (m->pref_pilot != slot->pref_pilot && s->phase == NET_PHASE_LOBBY) {
		slot->pref_pilot = m->pref_pilot;
		if (!net_pilot_taken(s, m->pref_pilot, index)) {
			slot->pilot = m->pref_pilot;
		}
		s->lobby_dirty = true;
	}

	slot->input = m->input;
}

static void net_host_drop_slot(net_session_t *s, int index) {
	net_slot_t *slot = &s->slots[index];
	s->stats.slots_dropped++;
	if (s->phase == NET_PHASE_LOBBY || s->phase == NET_PHASE_RESULTS) {
		memset(slot, 0, sizeof(*slot));
	}
	else {
		// Keep the slot during a race; the game hands the ship to the AI
		slot->connected = false;
		memset(&slot->input, 0, sizeof(slot->input));
	}
	s->lobby_dirty = true;
}

static void net_host_handle_packet(net_session_t *s, net_addr_t from, const uint8_t *buf, int len) {
	switch (net_peek_type(buf, len)) {
		case NET_MSG_DISCOVER: {
			net_msg_host_info_t info;
			memset(&info, 0, sizeof(info));
			info.session = s->session;
			info.phase = s->phase;
			info.num_players = net_session_num_players(s);
			info.max_players = NET_MAX_PLAYERS;
			net_copy_name(info.name, s->slots[0].name);
			uint8_t out[128];
			net_send(s, from, out, net_encode_host_info(out, sizeof(out), &info));
			break;
		}
		case NET_MSG_JOIN: {
			net_msg_join_t m;
			if (net_decode_join(buf, len, &m)) {
				net_host_handle_join(s, from, &m);
			}
			else {
				s->stats.malformed++;
			}
			break;
		}
		case NET_MSG_CLIENT_STATE: {
			net_msg_client_state_t m;
			if (net_decode_client_state(buf, len, &m)) {
				net_host_handle_client_state(s, from, &m);
			}
			else {
				s->stats.malformed++;
			}
			break;
		}
		case NET_MSG_LEAVE: {
			net_msg_leave_t m;
			if (net_decode_leave(buf, len, &m) && m.session == s->session) {
				// Sent several times; only the first one counts
				int index = net_host_find_slot(s, m.nonce);
				if (index >= 0 && s->slots[index].connected) {
					net_host_drop_slot(s, index);
				}
			}
			break;
		}
		default:
			// Some other host's traffic, or garbage
			if (net_peek_type(buf, len) == 0) {
				s->stats.malformed++;
			}
			break;
	}
}

static void net_host_update(net_session_t *s) {
	double timeout = s->phase == NET_PHASE_LOADING ? NET_TIMEOUT_LOADING : NET_TIMEOUT;
	for (int i = 1; i < NET_MAX_PLAYERS; i++) {
		net_slot_t *slot = &s->slots[i];
		if (slot->used && slot->connected && s->now - slot->last_recv > timeout) {
			net_host_drop_slot(s, i);
		}
	}

	if (s->lobby_dirty || s->now >= s->next_lobby_send) {
		net_host_send_lobby(s);
	}
}

void net_session_host_set_race(net_session_t *s, int race_class, int circuit) {
	s->race_class = race_class;
	s->circuit = circuit;
	s->lobby_dirty = true;
}

void net_session_host_set_pilot(net_session_t *s, int pilot) {
	s->slots[0].pref_pilot = pilot;
	if (!net_pilot_taken(s, pilot, 0)) {
		s->slots[0].pilot = pilot;
	}
	s->lobby_dirty = true;
}

void net_session_host_start_loading(net_session_t *s) {
	s->race_id++;
	s->num_results = 0;
	net_session_host_set_phase(s, NET_PHASE_LOADING);
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		memset(s->slots[i].presses_seen, 0, sizeof(s->slots[i].presses_seen));
		memset(s->slots[i].pressed, 0, sizeof(s->slots[i].pressed));
		// The counters continue where they are; don't fire on the first frame
		memcpy(s->slots[i].presses_seen, s->slots[i].input.presses, NET_NUM_ACTIONS);
	}
}

void net_session_host_set_phase(net_session_t *s, net_phase_t phase) {
	if (s->phase == phase) {
		return;
	}
	s->phase = phase;
	s->phase_time = s->now;

	// Players that left during the race are gone for good now
	if (phase == NET_PHASE_LOBBY || phase == NET_PHASE_RESULTS) {
		for (int i = 1; i < NET_MAX_PLAYERS; i++) {
			if (s->slots[i].used && !s->slots[i].connected) {
				memset(&s->slots[i], 0, sizeof(s->slots[i]));
			}
		}
	}
	s->lobby_dirty = true;
	net_host_send_lobby(s);
}

void net_session_host_set_results(net_session_t *s, const net_result_t *results, int num) {
	s->num_results = num < NET_MAX_SHIPS ? num : NET_MAX_SHIPS;
	memcpy(s->results, results, sizeof(net_result_t) * s->num_results);
	s->lobby_dirty = true;
}

bool net_session_host_all_ready(net_session_t *s) {
	if (s->ready_race_id != s->race_id) {
		return false; // the host itself
	}
	for (int i = 1; i < NET_MAX_PLAYERS; i++) {
		net_slot_t *slot = &s->slots[i];
		if (slot->used && slot->connected && slot->ready_race_id != s->race_id) {
			return false;
		}
	}
	return true;
}

void net_session_host_send_snapshot(net_session_t *s, const uint8_t *data, int len) {
	for (int i = 1; i < NET_MAX_PLAYERS; i++) {
		if (s->slots[i].used && s->slots[i].connected) {
			net_send(s, s->slots[i].addr, data, len);
		}
	}
}

void net_session_host_latch_presses(net_session_t *s) {
	for (int i = 1; i < NET_MAX_PLAYERS; i++) {
		net_slot_t *slot = &s->slots[i];
		for (int a = 0; a < NET_NUM_ACTIONS; a++) {
			slot->pressed[a] = slot->connected && slot->input.presses[a] != slot->presses_seen[a];
			slot->presses_seen[a] = slot->input.presses[a];
		}
	}
}


// Client --------------------------------------------------------------------

bool net_session_discover(net_session_t *s, uint16_t port) {
	net_session_init(s);
	if (!net_socket_open(&s->sock, 0, true)) {
		s->status = NET_STATUS_ERROR;
		return false;
	}
	s->role = NET_ROLE_DISCOVER;
	s->status = NET_STATUS_OK;
	s->port = port;
	s->start_time = s->now = net_time();
	s->next_discover = s->now;
	return true;
}

bool net_session_join(net_session_t *s, net_addr_t host, const char *name, int pref_pilot) {
	if (s->role != NET_ROLE_DISCOVER) {
		net_session_init(s);
		if (!net_socket_open(&s->sock, 0, true)) {
			s->status = NET_STATUS_ERROR;
			return false;
		}
		s->start_time = net_time();
	}
	s->role = NET_ROLE_CLIENT;
	s->status = NET_STATUS_JOINING;
	s->now = net_time();
	s->host_addr = host;
	s->last_host_recv = s->now;
	s->next_send = s->now;
	s->nonce = net_random_id(s);
	s->pref_pilot = pref_pilot % NET_MAX_SHIPS;
	s->my_slot = -1;
	s->session = 0;
	s->phase = NET_PHASE_LOBBY;
	s->have_snapshot = false;
	s->snapshot_new = false;
	net_copy_name(s->name, name);
	return true;
}

void net_session_client_set_ready(net_session_t *s, uint16_t race_id) {
	s->ready_race_id = race_id;
	s->next_send = s->now; // tell the host right away
}

void net_session_client_set_pilot(net_session_t *s, int pilot) {
	s->pref_pilot = pilot % NET_MAX_SHIPS;
	s->next_send = s->now;
}

bool net_session_client_in_lobby(net_session_t *s) {
	return s->role == NET_ROLE_CLIENT && s->status == NET_STATUS_OK && s->my_slot >= 0;
}

int net_session_client_take_snapshot(net_session_t *s, const uint8_t **data) {
	if (!s->snapshot_new) {
		return 0;
	}
	s->snapshot_new = false;
	*data = s->snapshot_buf;
	return s->snapshot_len;
}

static void net_client_send(net_session_t *s) {
	uint8_t buf[256];
	int len;
	if (s->status == NET_STATUS_JOINING) {
		net_msg_join_t m;
		memset(&m, 0, sizeof(m));
		m.nonce = s->nonce;
		m.pref_pilot = s->pref_pilot;
		net_copy_name(m.name, s->name);
		len = net_encode_join(buf, sizeof(buf), &m);
	}
	else {
		net_msg_client_state_t m;
		memset(&m, 0, sizeof(m));
		m.session = s->session;
		m.nonce = s->nonce;
		m.ready_race_id = s->ready_race_id;
		m.snapshot_ack = s->snapshot_seq;
		m.echo_time = s->echo_host_time > 0
			? s->echo_host_time + (float)(s->now - s->echo_recv_time)
			: 0;
		m.pref_pilot = s->pref_pilot;
		m.input = s->input;
		len = net_encode_client_state(buf, sizeof(buf), &m);
	}
	net_send(s, s->host_addr, buf, len);

	double interval = (s->phase == NET_PHASE_RACE || s->phase == NET_PHASE_LOADING)
		? NET_CLIENT_SEND_INTERVAL_RACE
		: NET_CLIENT_SEND_INTERVAL_LOBBY;
	if (s->status == NET_STATUS_JOINING) {
		interval = 0.25;
	}
	s->next_send = s->now + interval;
}

static void net_client_handle_lobby(net_session_t *s, const net_msg_lobby_t *m) {
	// A different session: the host was restarted. Start over.
	if (s->session != 0 && m->session != s->session) {
		s->my_slot = -1;
		s->status = NET_STATUS_JOINING;
		s->have_snapshot = false;
	}

	// Is this really meant for us?
	if (m->your_slot < 0 || m->slots[m->your_slot].nonce != s->nonce) {
		if (s->status == NET_STATUS_OK) {
			// We were dropped (time out); join again
			s->status = NET_STATUS_JOINING;
			s->my_slot = -1;
		}
		return;
	}

	s->session = m->session;
	s->status = NET_STATUS_OK;
	s->my_slot = m->your_slot;
	s->echo_host_time = m->host_time;
	s->echo_recv_time = s->now;

	if (s->phase != m->phase) {
		s->phase_time = s->now;
	}
	s->phase = m->phase;
	if (m->race_id != s->race_id) {
		// A new race: the snapshots of the last one are history
		s->have_snapshot = false;
		s->snapshot_new = false;
	}
	s->race_id = m->race_id;
	s->race_class = m->race_class;
	s->circuit = m->circuit;
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		net_slot_t *slot = &s->slots[i];
		const net_slot_info_t *info = &m->slots[i];
		slot->used = info->used;
		slot->connected = info->connected;
		slot->pilot = info->pilot;
		slot->ping_ms = info->ping_ms;
		slot->nonce = info->nonce;
		slot->ready_race_id = info->ready ? m->race_id : (uint16_t)(m->race_id - 1);
		memcpy(slot->name, info->name, NET_NAME_LEN);
	}
	s->num_results = m->num_results;
	memcpy(s->results, m->results, sizeof(s->results));
}

static void net_client_handle_packet(net_session_t *s, net_addr_t from, const uint8_t *buf, int len) {
	net_msg_type_t type = net_peek_type(buf, len);

	if (type == NET_MSG_HOST_INFO) {
		net_msg_host_info_t info;
		if (!net_decode_host_info(buf, len, &info)) {
			s->stats.malformed++;
			return;
		}
		int index = -1;
		for (int i = 0; i < s->found_len; i++) {
			if (net_addr_equal(s->found[i].addr, from)) {
				index = i;
			}
		}
		if (index < 0 && s->found_len < NET_MAX_FOUND_HOSTS) {
			index = s->found_len++;
		}
		if (index >= 0) {
			s->found[index].addr = from;
			s->found[index].info = info;
			s->found[index].last_seen = s->now;
		}
		return;
	}

	if (s->role != NET_ROLE_CLIENT || !net_addr_equal(from, s->host_addr)) {
		return;
	}

	switch (type) {
		case NET_MSG_LOBBY: {
			static net_msg_lobby_t m;
			if (net_decode_lobby(buf, len, &m)) {
				s->last_host_recv = s->now;
				net_client_handle_lobby(s, &m);
			}
			else {
				s->stats.malformed++;
			}
			break;
		}
		case NET_MSG_SNAPSHOT: {
			// Only the header is looked at here; the game decodes the rest
			net_reader_t r;
			net_reader_init(&r, buf, len);
			net_read_header(&r);
			uint32_t session = net_read_u32(&r);
			uint16_t race_id = net_read_u16(&r);
			uint32_t seq = net_read_u32(&r);
			float host_time = net_read_f32(&r);
			if (r.error || session != s->session || race_id != s->race_id || s->status != NET_STATUS_OK) {
				break;
			}
			s->last_host_recv = s->now;
			if (s->have_snapshot && !net_seq_newer(seq, s->snapshot_seq)) {
				s->stats.snapshots_late++;
				break;
			}
			if (s->have_snapshot) {
				uint32_t gap = seq - s->snapshot_seq;
				if (gap > 1) {
					s->stats.snapshots_missed += gap - 1;
				}
				double since = s->now - s->snapshot_recv_time;
				if (s->phase == NET_PHASE_RACE && since > s->stats.snapshot_max_gap) {
					s->stats.snapshot_max_gap = since;
				}
			}
			s->stats.snapshots++;
			memcpy(s->snapshot_buf, buf, len);
			s->snapshot_len = len;
			s->snapshot_seq = seq;
			s->snapshot_new = true;
			s->have_snapshot = true;
			s->snapshot_recv_time = s->now;
			s->echo_host_time = host_time;
			s->echo_recv_time = s->now;
			break;
		}
		case NET_MSG_REJECT: {
			net_msg_reject_t m;
			if (net_decode_reject(buf, len, &m) && s->status == NET_STATUS_JOINING) {
				s->status = NET_STATUS_REJECTED;
				s->reject_reason = m.reason;
			}
			break;
		}
		case NET_MSG_LEAVE: {
			net_msg_leave_t m;
			if (net_decode_leave(buf, len, &m) && m.session == s->session && s->session != 0) {
				s->status = NET_STATUS_HOST_LEFT;
			}
			break;
		}
		default:
			if (type == 0) {
				s->stats.malformed++;
			}
			break;
	}
}

static void net_client_update(net_session_t *s) {
	if (s->status == NET_STATUS_REJECTED || s->status == NET_STATUS_HOST_LEFT || s->status == NET_STATUS_HOST_LOST) {
		return;
	}

	double timeout = s->phase == NET_PHASE_LOADING ? NET_TIMEOUT_LOADING : NET_TIMEOUT;
	if (s->now - s->last_host_recv > timeout) {
		s->status = NET_STATUS_HOST_LOST;
		return;
	}

	if (s->now >= s->next_send) {
		net_client_send(s);
	}
}

static void net_discover_update(net_session_t *s) {
	if (s->now >= s->next_discover) {
		uint8_t buf[16];
		int len = net_encode_discover(buf, sizeof(buf));
		net_send(s, (net_addr_t){.ip = NET_ADDR_BROADCAST, .port = s->port}, buf, len);
		// Broadcasts don't always reach a host on the same machine
		net_send(s, (net_addr_t){.ip = NET_ADDR_LOOPBACK, .port = s->port}, buf, len);
		s->next_discover = s->now + NET_DISCOVER_INTERVAL;
	}

	for (int i = 0; i < s->found_len; i++) {
		if (s->now - s->found[i].last_seen > NET_FOUND_HOST_EXPIRE) {
			s->found[i] = s->found[--s->found_len];
			i--;
		}
	}
}


// Shared --------------------------------------------------------------------

void net_session_update(net_session_t *s, double now) {
	if (s->role == NET_ROLE_NONE) {
		return;
	}
	s->now = now;

	static uint8_t buf[NET_MAX_PACKET + 1];
	net_addr_t from;
	int len;
	int budget = 4096; // never get stuck here in a flood
	while (budget-- > 0 && (len = net_socket_recv(&s->sock, buf, sizeof(buf), &from)) > 0) {
		if (len > NET_MAX_PACKET) {
			s->stats.malformed++;
			continue;
		}
		s->stats.packets_received++;
		s->stats.bytes_received += len;
		if (s->role == NET_ROLE_HOST) {
			net_host_handle_packet(s, from, buf, len);
		}
		else {
			net_client_handle_packet(s, from, buf, len);
		}
	}

	if (s->role == NET_ROLE_HOST) {
		net_host_update(s);
	}
	else if (s->role == NET_ROLE_CLIENT) {
		net_client_update(s);
	}
	else if (s->role == NET_ROLE_DISCOVER) {
		net_discover_update(s);
	}
}

void net_session_close(net_session_t *s) {
	if (s->role == NET_ROLE_NONE) {
		return;
	}
	uint8_t buf[64];
	net_msg_leave_t m = {.session = s->session, .nonce = s->nonce};
	int len = net_encode_leave(buf, sizeof(buf), &m);

	// A few times; there's no ack
	for (int k = 0; k < 3; k++) {
		if (s->role == NET_ROLE_HOST) {
			for (int i = 1; i < NET_MAX_PLAYERS; i++) {
				if (s->slots[i].used && s->slots[i].connected) {
					net_socket_send(&s->sock, s->slots[i].addr, buf, len);
				}
			}
		}
		else if (s->role == NET_ROLE_CLIENT && s->session != 0) {
			net_socket_send(&s->sock, s->host_addr, buf, len);
		}
	}
	net_socket_close(&s->sock);
	s->role = NET_ROLE_NONE;
}
