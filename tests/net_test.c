// LAN protocol and session tests. No game data, no window: run anywhere.
//
//   make net_test && ./net-test
//
// The session tests run one host and up to eight clients in this process,
// talking over real UDP sockets on 127.0.0.1. Time is simulated (the session
// takes "now" as a parameter), so time outs can be tested without waiting.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "net.h"
#include "wipeout/net_proto.h"
#include "wipeout/net_session.h"

#if defined(_WIN32)
	#include <windows.h>
	static void sleep_ms(int ms) { Sleep(ms); }
#else
	#include <time.h>
	static void sleep_ms(int ms) { struct timespec ts = {0, ms * 1000000L}; nanosleep(&ts, NULL); }
#endif

static int tests_run = 0;
static int tests_failed = 0;
static int checks_failed = 0;

#define CHECK(COND) do { \
	if (!(COND)) { \
		printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #COND); \
		checks_failed++; \
	} \
} while (0)

#define CHECK_EQ_INT(A, B) do { \
	long long _a = (long long)(A), _b = (long long)(B); \
	if (_a != _b) { \
		printf("    FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #A, #B, _a, _b); \
		checks_failed++; \
	} \
} while (0)

static void run_test(const char *name, void (*fn)(void)) {
	int before = checks_failed;
	printf("  %s\n", name);
	fn();
	tests_run++;
	if (checks_failed != before) {
		tests_failed++;
	}
}

static uint32_t rng_state = 0x9e3779b9;
static uint32_t rnd(void) {
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}
static float rndf(float lo, float hi) {
	return lo + (hi - lo) * ((rnd() & 0xffffff) / (float)0xffffff);
}
static vec3_t rndv(void) {
	return (vec3_t){rndf(-50000, 50000), rndf(-50000, 50000), rndf(-50000, 50000)};
}


// -----------------------------------------------------------------------------
// Protocol

static void fill_snapshot(net_snapshot_t *s) {
	memset(s, 0, sizeof(*s));
	s->session = rnd();
	s->race_id = rnd();
	s->seq = rnd();
	s->host_time = rndf(0, 1000);
	s->num_ships = NET_MAX_SHIPS;
	for (int i = 0; i < s->num_ships; i++) {
		net_ship_state_t *sh = &s->ships[i];
		sh->flags = rnd();
		sh->section = rnd() % 500;
		sh->lap = (int)(rnd() % 5) - 1;
		sh->rank = 1 + rnd() % 8;
		sh->weapon_type = rnd() % 10;
		sh->weapon_target = (int)(rnd() % 9) - 1;
		sh->brake_left = rnd();
		sh->brake_right = rnd();
		sh->position = rndv();
		sh->velocity = rndv();
		sh->angle = rndv();
		sh->angular_velocity = rndv();
		sh->speed = rndf(0, 40000);
		sh->thrust_mag = rndf(0, 3000);
		sh->lap_time = rndf(0, 100);
		sh->update_timer = rndf(-1, 10);
		sh->turbo_timer = rndf(0, 2);
		for (int l = 0; l < NET_NUM_LAPS; l++) {
			sh->lap_times[l] = rndf(0, 100);
		}
	}
	s->num_weapons = NET_MAX_WEAPONS;
	for (int i = 0; i < s->num_weapons; i++) {
		net_weapon_state_t *w = &s->weapons[i];
		w->kind = 1 + rnd() % (NET_WEAPON_KIND_MAX - 1);
		w->owner = rnd() % NET_MAX_SHIPS;
		w->position = rndv();
		w->angle = rndv();
		w->velocity = rndv();
		w->timer = rndf(0, 15);
	}
	s->num_droids = NET_MAX_PLAYERS;
	for (int i = 0; i < s->num_droids; i++) {
		s->droids[i].pilot = i;
		s->droids[i].tractor = rnd() & 1;
		s->droids[i].position = rndv();
		s->droids[i].angle = rndv();
	}
	s->num_pickups = 37;
	for (int i = 0; i < (s->num_pickups + 7) / 8; i++) {
		s->pickups[i] = rnd();
	}
	s->pickups[(s->num_pickups - 1) / 8] &= (1 << (s->num_pickups % 8)) - 1;
	s->num_events = NET_MAX_EVENTS;
	for (int i = 0; i < s->num_events; i++) {
		net_event_t *e = &s->events[i];
		e->seq = rnd();
		e->type = 1 + rnd() % (NET_EVENT_MAX - 1);
		e->target = (int)(rnd() % 9) - 1;
		e->a = rnd();
		e->b = rnd();
		e->pos = rndv();
		e->vec = rndv();
		e->value = rndf(-10, 10);
	}
}

static void test_snapshot_roundtrip(void) {
	static net_snapshot_t a, b;
	static uint8_t buf[NET_MAX_PACKET];
	for (int iter = 0; iter < 50; iter++) {
		fill_snapshot(&a);
		int len = net_encode_snapshot(buf, sizeof(buf), &a);
		CHECK(len > 0);
		CHECK(len <= NET_MAX_PACKET);
		memset(&b, 0xcd, sizeof(b));
		CHECK(net_decode_snapshot(buf, len, &b));
		CHECK_EQ_INT(b.session, a.session);
		CHECK_EQ_INT(b.seq, a.seq);
		CHECK_EQ_INT(b.race_id, a.race_id);
		CHECK_EQ_INT(b.num_ships, a.num_ships);
		CHECK_EQ_INT(b.num_weapons, a.num_weapons);
		CHECK_EQ_INT(b.num_events, a.num_events);
		CHECK_EQ_INT(b.num_pickups, a.num_pickups);
		CHECK(memcmp(b.pickups, a.pickups, (a.num_pickups + 7) / 8) == 0);
		for (int i = 0; i < a.num_ships; i++) {
			CHECK_EQ_INT(b.ships[i].flags, a.ships[i].flags);
			CHECK_EQ_INT(b.ships[i].section, a.ships[i].section);
			CHECK_EQ_INT(b.ships[i].lap, a.ships[i].lap);
			CHECK_EQ_INT(b.ships[i].weapon_target, a.ships[i].weapon_target);
			CHECK(memcmp(&b.ships[i].position, &a.ships[i].position, sizeof(vec3_t)) == 0);
			CHECK(b.ships[i].lap_times[2] == a.ships[i].lap_times[2]);
		}
		for (int i = 0; i < a.num_weapons; i++) {
			CHECK_EQ_INT(b.weapons[i].kind, a.weapons[i].kind);
			CHECK(b.weapons[i].timer == a.weapons[i].timer);
		}
		for (int i = 0; i < a.num_events; i++) {
			CHECK_EQ_INT(b.events[i].seq, a.events[i].seq);
			CHECK_EQ_INT(b.events[i].target, a.events[i].target);
			CHECK(b.events[i].value == a.events[i].value);
		}
	}

	// The worst case snapshot must fit into a single datagram buffer
	printf("    full snapshot: %d bytes\n", net_encode_snapshot(buf, sizeof(buf), &a));
}

static void test_messages_roundtrip(void) {
	uint8_t buf[NET_MAX_PACKET];
	int len;

	net_msg_join_t join = {.nonce = 1234, .pref_pilot = 5}, join2;
	strcpy(join.name, "Fab-1.x");
	len = net_encode_join(buf, sizeof(buf), &join);
	CHECK(net_decode_join(buf, len, &join2));
	CHECK_EQ_INT(join2.nonce, 1234);
	CHECK_EQ_INT(join2.pref_pilot, 5);
	CHECK(strcmp(join2.name, "FAB 1 X") == 0); // only what the UI font can draw

	net_msg_client_state_t cs, cs2;
	memset(&cs, 0, sizeof(cs));
	cs.session = 77; cs.nonce = 88; cs.ready_race_id = 9; cs.snapshot_ack = 1000; cs.echo_time = 12.5f; cs.pref_pilot = 7;
	for (int i = 0; i < NET_NUM_ACTIONS; i++) { cs.input.analog[i] = i * 20; cs.input.presses[i] = 200 + i; }
	cs.input.analog_response = 30;
	len = net_encode_client_state(buf, sizeof(buf), &cs);
	CHECK(net_decode_client_state(buf, len, &cs2));
	CHECK(memcmp(&cs.input, &cs2.input, sizeof(cs.input)) == 0);
	CHECK_EQ_INT(cs2.snapshot_ack, 1000);
	CHECK(cs2.echo_time == 12.5f);

	static net_msg_lobby_t lobby, lobby2;
	memset(&lobby, 0, sizeof(lobby));
	lobby.session = 5; lobby.phase = NET_PHASE_RESULTS; lobby.race_id = 65535; lobby.your_slot = 3;
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		lobby.slots[i].used = 1; lobby.slots[i].pilot = i; lobby.slots[i].nonce = 100 + i; lobby.slots[i].ping_ms = i * 3;
		sprintf(lobby.slots[i].name, "P%d", i);
	}
	lobby.num_results = 8;
	for (int i = 0; i < 8; i++) {
		lobby.results[i].pilot = 7 - i; lobby.results[i].slot = i - 1; lobby.results[i].race_time = 90 + i;
	}
	len = net_encode_lobby(buf, sizeof(buf), &lobby);
	CHECK(net_decode_lobby(buf, len, &lobby2));
	CHECK_EQ_INT(lobby2.your_slot, 3);
	CHECK_EQ_INT(lobby2.race_id, 65535);
	CHECK_EQ_INT(lobby2.results[0].slot, -1);
	CHECK(strcmp(lobby2.slots[7].name, "P7") == 0);

	// A wrong version is not ours
	len = net_encode_lobby(buf, sizeof(buf), &lobby);
	buf[2] = NET_PROTOCOL_VERSION + 1;
	CHECK(!net_decode_lobby(buf, len, &lobby2));
	CHECK_EQ_INT(net_peek_type(buf, len), 0);

	// Bad values are refused
	net_msg_join_t bad = {.nonce = 1, .pref_pilot = 8};
	len = net_encode_join(buf, sizeof(buf), &bad);
	CHECK(!net_decode_join(buf, len, &join2));

	// Too small a buffer: no partial packet
	CHECK_EQ_INT(net_encode_lobby(buf, 20, &lobby), 0);
}

static void test_names(void) {
	struct { const char *in, *out; } cases[] = {
		{"Fab-Asus", "FAB ASUS"},
		{"fab_asus--pc", "FAB ASUS P"},
		{"  --Fab  ", "FAB"},
		{"DESKTOP-4F2K9QZ", "DESKTOP 4F"},
		{"\xc3\xa9t\xc3\xa9", "T"},
		{"!!!", "PLAYER"},
		{"", "PLAYER"},
		{"ABCDEFGHIJKLMNOP", "ABCDEFGHIJ"},
	};
	for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
		char name[NET_NAME_LEN];
		net_sanitize_name(name, cases[i].in);
		if (strcmp(name, cases[i].out) != 0) {
			printf("    \"%s\" -> \"%s\", expected \"%s\"\n", cases[i].in, name, cases[i].out);
			checks_failed++;
		}
		CHECK((int)strlen(name) <= NET_NAME_MAX_CHARS);
	}

	// Whatever comes over the network ends up clean as well
	uint8_t buf[256];
	net_msg_join_t join = {.nonce = 1, .pref_pilot = 0}, out;
	memcpy(join.name, "a-b.c_d#e\x01\xff", 12);
	int len = net_encode_join(buf, sizeof(buf), &join);
	CHECK(net_decode_join(buf, len, &out));
	CHECK(strcmp(out.name, "A B C D E") == 0);
}

static void test_snapshot_rejects_non_finite(void) {
	static net_snapshot_t a, b;
	static uint8_t buf[NET_MAX_PACKET];
	fill_snapshot(&a);
	a.ships[3].position.y = NAN;
	int len = net_encode_snapshot(buf, sizeof(buf), &a);
	CHECK(len > 0);
	CHECK(!net_decode_snapshot(buf, len, &b));
	fill_snapshot(&a);
	a.events[0].value = INFINITY;
	len = net_encode_snapshot(buf, sizeof(buf), &a);
	CHECK(!net_decode_snapshot(buf, len, &b));
}

// Random and mutated packets: the decoders must never read out of bounds or
// accept garbage with out of range values. Built with -fsanitize=address on
// Linux this also proves the absence of overreads.
static void test_fuzz_decoders(void) {
	static net_snapshot_t snap, out;
	static net_msg_lobby_t lobby;
	static uint8_t valid[NET_MAX_PACKET], buf[NET_MAX_PACKET + 64];
	int accepted = 0;

	for (int iter = 0; iter < 200000; iter++) {
		int len;
		int mode = rnd() % 4;
		if (mode == 0) {
			// Pure noise, with a valid header now and then
			len = rnd() % 600;
			for (int i = 0; i < len; i++) {
				buf[i] = rnd();
			}
			if (len >= 4 && (rnd() & 1)) {
				buf[0] = NET_PROTOCOL_MAGIC & 0xff;
				buf[1] = NET_PROTOCOL_MAGIC >> 8;
				buf[2] = NET_PROTOCOL_VERSION;
				buf[3] = 1 + rnd() % (NET_MSG_MAX - 1);
			}
		}
		else {
			// A valid snapshot or lobby, then flipped bits, cut or extended
			if (iter % 50 == 0 || mode == 1) {
				fill_snapshot(&snap);
				snap.num_weapons = rnd() % 8;
				snap.num_events = rnd() % 8;
				len = net_encode_snapshot(valid, sizeof(valid), &snap);
			}
			else {
				memset(&lobby, 0, sizeof(lobby));
				lobby.your_slot = rnd() % 8;
				lobby.num_results = rnd() % 9;
				len = net_encode_lobby(valid, sizeof(valid), &lobby);
			}
			memcpy(buf, valid, len);
			int flips = 1 + rnd() % 6;
			for (int f = 0; f < flips && len > 0; f++) {
				buf[rnd() % len] ^= 1 << (rnd() % 8);
			}
			if (mode == 2 && len > 0) {
				len = rnd() % len;
			}
			if (mode == 3) {
				int extra = rnd() % 64;
				for (int i = 0; i < extra; i++) {
					buf[len + i] = rnd();
				}
				len += extra;
			}
		}

		net_msg_host_info_t hi;
		net_msg_join_t j;
		net_msg_client_state_t cs;
		net_msg_leave_t lv;
		net_msg_reject_t rj;
		net_peek_type(buf, len);
		net_decode_host_info(buf, len, &hi);
		net_decode_join(buf, len, &j);
		net_decode_client_state(buf, len, &cs);
		net_decode_leave(buf, len, &lv);
		net_decode_reject(buf, len, &rj);
		if (net_decode_lobby(buf, len, &lobby)) {
			CHECK(lobby.your_slot >= -1 && lobby.your_slot < NET_MAX_PLAYERS);
			CHECK(lobby.num_results <= NET_MAX_SHIPS);
			for (int i = 0; i < NET_MAX_PLAYERS; i++) {
				CHECK(lobby.slots[i].pilot < NET_MAX_SHIPS);
				CHECK(lobby.slots[i].name[NET_NAME_LEN - 1] == '\0');
			}
		}
		if (net_decode_snapshot(buf, len, &out)) {
			accepted++;
			CHECK(out.num_ships <= NET_MAX_SHIPS);
			CHECK(out.num_weapons <= NET_MAX_WEAPONS);
			CHECK(out.num_events <= NET_MAX_EVENTS);
			CHECK(out.num_pickups <= NET_MAX_PICKUPS);
			for (int i = 0; i < out.num_ships; i++) {
				CHECK(out.ships[i].section >= 0);
				CHECK(out.ships[i].weapon_target >= -1 && out.ships[i].weapon_target < NET_MAX_SHIPS);
				CHECK(isfinite(out.ships[i].position.x) && isfinite(out.ships[i].speed));
			}
			for (int i = 0; i < out.num_weapons; i++) {
				CHECK(out.weapons[i].kind > 0 && out.weapons[i].kind < NET_WEAPON_KIND_MAX);
				CHECK(out.weapons[i].owner < NET_MAX_SHIPS);
			}
			for (int i = 0; i < out.num_events; i++) {
				CHECK(out.events[i].type > 0 && out.events[i].type < NET_EVENT_MAX);
			}
		}
		if (checks_failed > 20) {
			break;
		}
	}
	printf("    200000 packets, %d mutated snapshots still valid (value bits flipped)\n", accepted);
}


// -----------------------------------------------------------------------------
// Session

#define TEST_PORT_BASE 47900
static int test_port = TEST_PORT_BASE;

typedef struct {
	net_session_t host;
	net_session_t clients[NET_MAX_PLAYERS + 1];
	int num_clients;
	double now;
	bool frozen[NET_MAX_PLAYERS + 1]; // stops updating (crashed / unplugged)
	bool host_frozen;
} lan_t;

static lan_t lan;

// Advance simulated time in steps; give the loopback a moment to deliver
static void pump(double seconds, double step) {
	int steps = (int)(seconds / step + 0.5);
	for (int s = 0; s < steps; s++) {
		lan.now += step;
		if (!lan.host_frozen) {
			net_session_update(&lan.host, lan.now);
		}
		for (int i = 0; i < lan.num_clients; i++) {
			if (!lan.frozen[i]) {
				net_session_update(&lan.clients[i], lan.now);
			}
		}
		if ((s & 3) == 0) {
			sleep_ms(1);
		}
	}
}

static void lan_setup(int num_clients, float loss, float duplicate) {
	memset(&lan, 0, sizeof(lan));
	lan.now = net_time();
	int port = test_port++;
	lan.host.sim.loss = loss;
	lan.host.sim.duplicate = duplicate;
	CHECK(net_session_host(&lan.host, port, "HOST", 0, 0, 0));
	net_addr_t addr = {.ip = NET_ADDR_LOOPBACK, .port = port};
	lan.num_clients = num_clients;
	for (int i = 0; i < num_clients; i++) {
		lan.clients[i].sim.loss = loss;
		lan.clients[i].sim.duplicate = duplicate;
		char name[16];
		sprintf(name, "C%d", i);
		CHECK(net_session_join(&lan.clients[i], addr, name, i % NET_MAX_SHIPS));
	}
}

static void lan_teardown(void) {
	net_session_close(&lan.host);
	for (int i = 0; i < lan.num_clients; i++) {
		net_session_close(&lan.clients[i]);
	}
}

static int count_joined(void) {
	int n = 0;
	for (int i = 0; i < lan.num_clients; i++) {
		if (lan.clients[i].status == NET_STATUS_OK && lan.clients[i].my_slot > 0) {
			n++;
		}
	}
	return n;
}

static void test_session_eight_players(void) {
	lan_setup(7, 0, 0);
	pump(1.0, 0.01);
	CHECK_EQ_INT(count_joined(), 7);
	CHECK_EQ_INT(net_session_num_players(&lan.host), 8);

	// Every slot and every pilot exactly once
	int slot_seen = 0, pilot_seen = 0;
	for (int i = 0; i < NET_MAX_PLAYERS; i++) {
		CHECK(lan.host.slots[i].used);
		pilot_seen |= 1 << lan.host.slots[i].pilot;
	}
	for (int i = 0; i < lan.num_clients; i++) {
		slot_seen |= 1 << lan.clients[i].my_slot;
		// The client's mirror of the lobby matches the host
		for (int s = 0; s < NET_MAX_PLAYERS; s++) {
			CHECK_EQ_INT(lan.clients[i].slots[s].pilot, lan.host.slots[s].pilot);
			CHECK_EQ_INT(lan.clients[i].slots[s].nonce, lan.host.slots[s].nonce);
		}
	}
	CHECK_EQ_INT(slot_seen, 0xfe);
	CHECK_EQ_INT(pilot_seen, 0xff);

	// Client 0 wanted pilot 0, which the host has: it got another one
	CHECK(lan.host.slots[lan.clients[0].my_slot].pilot != 0);

	// A ninth player is turned away
	net_session_t extra;
	memset(&extra, 0, sizeof(extra));
	net_session_join(&extra, (net_addr_t){.ip = NET_ADDR_LOOPBACK, .port = lan.host.port}, "LATE", 3);
	for (int s = 0; s < 100 && extra.status == NET_STATUS_JOINING; s++) {
		lan.now += 0.01;
		net_session_update(&lan.host, lan.now);
		net_session_update(&extra, lan.now);
		sleep_ms(1);
	}
	CHECK_EQ_INT(extra.status, NET_STATUS_REJECTED);
	CHECK_EQ_INT(extra.reject_reason, NET_REJECT_FULL);
	net_session_close(&extra);
	lan_teardown();
}

static void test_session_pilot_change(void) {
	lan_setup(2, 0, 0);
	pump(0.5, 0.01);
	CHECK_EQ_INT(count_joined(), 2);
	int a = lan.clients[0].my_slot, b = lan.clients[1].my_slot;

	// Client 1 asks for client 0's pilot: refused, keeps its own
	int pilot_a = lan.host.slots[a].pilot;
	int pilot_b = lan.host.slots[b].pilot;
	net_session_client_set_pilot(&lan.clients[1], pilot_a);
	pump(0.5, 0.01);
	CHECK_EQ_INT(lan.host.slots[b].pilot, pilot_b);

	// A free one is granted, and mirrored to everybody
	net_session_client_set_pilot(&lan.clients[1], 6);
	pump(0.5, 0.01);
	CHECK_EQ_INT(lan.host.slots[b].pilot, 6);
	CHECK_EQ_INT(lan.clients[0].slots[b].pilot, 6);
	lan_teardown();
}

static void test_session_race_handshake(void) {
	lan_setup(7, 0.1, 0.05);
	pump(2.0, 0.01);
	CHECK_EQ_INT(count_joined(), 7);

	net_session_host_start_loading(&lan.host);
	pump(0.5, 0.01);
	for (int i = 0; i < lan.num_clients; i++) {
		CHECK_EQ_INT(lan.clients[i].phase, NET_PHASE_LOADING);
		CHECK_EQ_INT(lan.clients[i].race_id, lan.host.race_id);
	}
	CHECK(!net_session_host_all_ready(&lan.host));

	// Loading takes a while; long beyond the normal time out
	lan.host_frozen = true;
	for (int i = 0; i < lan.num_clients; i++) {
		lan.frozen[i] = true;
	}
	lan.now += 20.0;
	lan.host_frozen = false;
	for (int i = 0; i < lan.num_clients; i++) {
		lan.frozen[i] = false;
	}

	net_session_client_set_ready(&lan.host, lan.host.race_id);
	for (int i = 0; i < lan.num_clients; i++) {
		pump(0.05, 0.01);
		CHECK(!net_session_host_all_ready(&lan.host));
		net_session_client_set_ready(&lan.clients[i], lan.clients[i].race_id);
	}
	pump(0.5, 0.01);
	CHECK(net_session_host_all_ready(&lan.host));
	CHECK_EQ_INT(lan.host.stats.slots_dropped, 0);

	net_session_host_set_phase(&lan.host, NET_PHASE_RACE);
	pump(0.3, 0.01);
	for (int i = 0; i < lan.num_clients; i++) {
		CHECK_EQ_INT(lan.clients[i].phase, NET_PHASE_RACE);
	}

	// Snapshots with 10% loss and 5% duplicates: the clients only ever take
	// newer ones
	static net_snapshot_t snap;
	static uint8_t buf[NET_MAX_PACKET];
	uint32_t last_seq[NET_MAX_PLAYERS + 1] = {0};
	int taken[NET_MAX_PLAYERS + 1] = {0};
	for (int frame = 0; frame < 600; frame++) {
		memset(&snap, 0, sizeof(snap));
		snap.session = lan.host.session;
		snap.race_id = lan.host.race_id;
		snap.seq = ++lan.host.snapshot_send_seq;
		snap.num_ships = NET_MAX_SHIPS;
		int len = net_encode_snapshot(buf, sizeof(buf), &snap);
		net_session_host_send_snapshot(&lan.host, buf, len);
		pump(1.0 / 60.0, 1.0 / 60.0);
		for (int i = 0; i < lan.num_clients; i++) {
			const uint8_t *data;
			int got = net_session_client_take_snapshot(&lan.clients[i], &data);
			if (got) {
				static net_snapshot_t in;
				CHECK(net_decode_snapshot(data, got, &in));
				CHECK(in.seq > last_seq[i]);
				last_seq[i] = in.seq;
				taken[i]++;
			}
		}
	}
	for (int i = 0; i < lan.num_clients; i++) {
		net_stats_t *st = &lan.clients[i].stats;
		CHECK(taken[i] > 450); // ~90% of 600
		CHECK(st->snapshots_late > 0 || st->snapshots_missed > 0);
		CHECK(lan.clients[i].status == NET_STATUS_OK);
	}
	printf("    client 0: %d of 600 snapshots, %u missed, %u late/duplicate\n",
		taken[0], lan.clients[0].stats.snapshots_missed, lan.clients[0].stats.snapshots_late);
	lan_teardown();
}

static void test_session_presses_survive_loss(void) {
	lan_setup(3, 0.25, 0);
	pump(2.0, 0.01);
	CHECK_EQ_INT(count_joined(), 3);
	net_session_host_start_loading(&lan.host);
	net_session_host_set_phase(&lan.host, NET_PHASE_RACE);
	pump(0.2, 0.01);

	// Each client presses FIRE (action 7) 100 times, every 6th frame; the host
	// latches once per frame like the game does
	int presses_seen[NET_MAX_PLAYERS] = {0};
	for (int frame = 0; frame < 100 * 6 + 120; frame++) {
		if (frame < 600 && frame % 6 == 0) {
			for (int i = 0; i < lan.num_clients; i++) {
				lan.clients[i].input.presses[7]++;
			}
		}
		pump(1.0 / 60.0, 1.0 / 60.0);
		net_session_host_latch_presses(&lan.host);
		for (int s = 1; s < NET_MAX_PLAYERS; s++) {
			if (lan.host.slots[s].pressed[7]) {
				presses_seen[s]++;
			}
		}
	}
	for (int i = 0; i < lan.num_clients; i++) {
		int seen = presses_seen[lan.clients[i].my_slot];
		// With 25% loss two presses 100 ms apart only merge when ~6 packets in a
		// row are lost: practically never
		CHECK(seen >= 98 && seen <= 100);
		if (i == 0) {
			printf("    25%% loss: %d of 100 presses arrived as presses\n", seen);
		}
	}
	lan_teardown();
}

static void test_session_timeouts(void) {
	lan_setup(3, 0, 0);
	pump(0.5, 0.01);
	CHECK_EQ_INT(count_joined(), 3);

	// A client that stops talking is dropped from the lobby
	lan.frozen[1] = true;
	pump(NET_TIMEOUT + 1.0, 0.05);
	CHECK_EQ_INT(net_session_num_players(&lan.host), 3);
	CHECK_EQ_INT(lan.host.stats.slots_dropped, 1);

	// ... and joins again by itself when it comes back
	lan.frozen[1] = false;
	pump(1.5, 0.01);
	CHECK_EQ_INT(count_joined(), 3);
	CHECK_EQ_INT(net_session_num_players(&lan.host), 4);

	// During a race the slot stays, marked as disconnected
	net_session_host_start_loading(&lan.host);
	net_session_host_set_phase(&lan.host, NET_PHASE_RACE);
	pump(0.2, 0.01);
	int slot = lan.clients[2].my_slot;
	lan.frozen[2] = true;
	pump(NET_TIMEOUT + 1.0, 0.05);
	CHECK(lan.host.slots[slot].used);
	CHECK(!lan.host.slots[slot].connected);
	// Its input is neutral from now on
	CHECK_EQ_INT(lan.host.slots[slot].input.analog[6], 0);

	// Back in the lobby it's gone
	net_session_host_set_phase(&lan.host, NET_PHASE_LOBBY);
	CHECK(!lan.host.slots[slot].used);

	// A host that disappears is noticed by the clients
	lan.host_frozen = true;
	lan.frozen[2] = true;
	pump(NET_TIMEOUT + 1.0, 0.05);
	CHECK_EQ_INT(lan.clients[0].status, NET_STATUS_HOST_LOST);
	CHECK_EQ_INT(lan.clients[1].status, NET_STATUS_HOST_LOST);
	lan_teardown();
}

static void test_session_leave_and_join_in_race(void) {
	lan_setup(2, 0, 0);
	pump(0.5, 0.01);

	// A clean goodbye frees the slot at once
	net_session_close(&lan.clients[1]);
	lan.frozen[1] = true;
	pump(0.2, 0.01);
	CHECK_EQ_INT(net_session_num_players(&lan.host), 2);

	// Nobody gets in while racing
	net_session_host_start_loading(&lan.host);
	net_session_host_set_phase(&lan.host, NET_PHASE_RACE);
	net_session_t late;
	memset(&late, 0, sizeof(late));
	net_session_join(&late, (net_addr_t){.ip = NET_ADDR_LOOPBACK, .port = lan.host.port}, "LATE", 1);
	for (int s = 0; s < 100 && late.status == NET_STATUS_JOINING; s++) {
		lan.now += 0.01;
		net_session_update(&lan.host, lan.now);
		net_session_update(&late, lan.now);
		sleep_ms(1);
	}
	CHECK_EQ_INT(late.status, NET_STATUS_REJECTED);
	CHECK_EQ_INT(late.reject_reason, NET_REJECT_IN_RACE);
	net_session_close(&late);

	// The host ends the game: the client hears about it
	net_session_close(&lan.host);
	lan.host_frozen = true;
	pump(0.2, 0.01);
	CHECK_EQ_INT(lan.clients[0].status, NET_STATUS_HOST_LEFT);
	lan_teardown();
}

static void test_session_ignores_strangers(void) {
	lan_setup(1, 0, 0);
	pump(0.5, 0.01);

	// Garbage and foreign packets at the host and the client
	net_socket_t sock;
	CHECK(net_socket_open(&sock, 0, false));
	uint8_t junk[512];
	for (int i = 0; i < 500; i++) {
		int len = 1 + rnd() % sizeof(junk);
		for (int b = 0; b < len; b++) {
			junk[b] = rnd();
		}
		if (i & 1) {
			junk[0] = NET_PROTOCOL_MAGIC & 0xff; junk[1] = NET_PROTOCOL_MAGIC >> 8;
			junk[2] = NET_PROTOCOL_VERSION; junk[3] = 1 + rnd() % (NET_MSG_MAX - 1);
		}
		net_socket_send(&sock, (net_addr_t){.ip = NET_ADDR_LOOPBACK, .port = lan.host.port}, junk, len);
		net_socket_send(&sock, (net_addr_t){.ip = NET_ADDR_LOOPBACK, .port = net_socket_port(&lan.clients[0].sock)}, junk, len);
	}

	// A fake lobby from somebody else, not from the host's address
	net_msg_lobby_t fake;
	memset(&fake, 0, sizeof(fake));
	fake.session = 0xdead;
	fake.phase = NET_PHASE_RACE;
	fake.your_slot = lan.clients[0].my_slot;
	fake.slots[fake.your_slot].nonce = lan.clients[0].nonce;
	int len = net_encode_lobby(junk, sizeof(junk), &fake);
	net_socket_send(&sock, (net_addr_t){.ip = NET_ADDR_LOOPBACK, .port = net_socket_port(&lan.clients[0].sock)}, junk, len);

	pump(0.5, 0.01);
	net_socket_close(&sock);
	CHECK_EQ_INT(lan.clients[0].status, NET_STATUS_OK);
	CHECK_EQ_INT(lan.clients[0].phase, NET_PHASE_LOBBY);
	CHECK(lan.clients[0].session == lan.host.session);
	CHECK_EQ_INT(net_session_num_players(&lan.host), 2);
	CHECK(lan.host.stats.malformed > 0);
	lan_teardown();
}

static void test_discovery(void) {
	lan_setup(0, 0, 0);
	net_session_t finder;
	memset(&finder, 0, sizeof(finder));
	CHECK(net_session_discover(&finder, lan.host.port));
	for (int s = 0; s < 200 && finder.found_len == 0; s++) {
		lan.now += 0.01;
		net_session_update(&lan.host, lan.now);
		net_session_update(&finder, lan.now);
		sleep_ms(1);
	}
	CHECK(finder.found_len >= 1);
	if (finder.found_len) {
		CHECK(strcmp(finder.found[0].info.name, "HOST") == 0);
		CHECK_EQ_INT(finder.found[0].info.num_players, 1);
	}

	// Join from the list
	if (finder.found_len) {
		CHECK(net_session_join(&finder, finder.found[0].addr, "FINDER", 2));
		for (int s = 0; s < 100 && finder.status != NET_STATUS_OK; s++) {
			lan.now += 0.01;
			net_session_update(&lan.host, lan.now);
			net_session_update(&finder, lan.now);
			sleep_ms(1);
		}
		CHECK_EQ_INT(finder.status, NET_STATUS_OK);
	}
	net_session_close(&finder);
	lan_teardown();
}

int main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IONBF, 0);
	if (!net_init()) {
		printf("no network\n");
		return 1;
	}
	rng_state ^= (uint32_t)(net_time() * 1000);
	test_port = TEST_PORT_BASE + (int)(rnd() % 1000);

	printf("protocol\n");
	run_test("messages round trip", test_messages_roundtrip);
	run_test("player names", test_names);
	run_test("snapshot round trip", test_snapshot_roundtrip);
	run_test("snapshot rejects NaN / inf", test_snapshot_rejects_non_finite);
	run_test("fuzzed decoders", test_fuzz_decoders);

	printf("session (UDP on 127.0.0.1)\n");
	run_test("8 players: slots, pilots, lobby full", test_session_eight_players);
	run_test("pilot change", test_session_pilot_change);
	run_test("race handshake + snapshots with 10% loss", test_session_race_handshake);
	run_test("button presses with 25% loss", test_session_presses_survive_loss);
	run_test("time outs, rejoin, host lost", test_session_timeouts);
	run_test("leave, join during a race, host ends", test_session_leave_and_join_in_race);
	run_test("garbage and foreign packets", test_session_ignores_strangers);
	run_test("LAN discovery", test_discovery);

	net_cleanup();
	printf("\n%d tests, %d failed (%d failed checks)\n", tests_run, tests_failed, checks_failed);
	return tests_failed ? 1 : 0;
}
