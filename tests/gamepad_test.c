// Two Xbox gamepads, end to end, without the pads: SDL virtual joysticks that
// look like Xbox controllers go through the real SDL event code
// (platform_sdl.c) and input.c, with the bindings of game_init() and the
// default controls of both players. No window, no game data.
//
//   make gamepad_test && ./gamepad-test
//
// Needs SDL >= 2.24 (virtual game controllers).

#define main platform_sdl_main
#include "../src/platform_sdl.c"
#undef main

#include "../src/wipeout/game.h"

// platform_sdl.c calls these from its main(), which isn't used here
void system_set_args(int argc, char **argv) {}
void system_init(void) {}
void system_update(void) {}
void system_cleanup(void) {}
void system_resize(vec2i_t size) {}

static int tests_run = 0;
static int tests_failed = 0;
static int checks_failed = 0;

#define CHECK(COND) do { \
	if (!(COND)) { \
		printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #COND); \
		checks_failed++; \
	} \
} while (0)

#define CHECK_NEAR(A, B) do { \
	float _a = (A), _b = (B); \
	if (fabsf(_a - _b) > 0.02) { \
		printf("    FAIL %s:%d: %s == %s (%.3f != %.3f)\n", __FILE__, __LINE__, #A, #B, _a, _b); \
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


// -----------------------------------------------------------------------------
// Bindings, as in game_init() with the default save.dat / controls2.dat

static const uint8_t p1_buttons[NUM_GAME_ACTIONS][2] = {
	[A_UP] = {INPUT_KEY_UP, INPUT_GAMEPAD_DPAD_UP},
	[A_DOWN] = {INPUT_KEY_DOWN, INPUT_GAMEPAD_DPAD_DOWN},
	[A_LEFT] = {INPUT_KEY_LEFT, INPUT_GAMEPAD_DPAD_LEFT},
	[A_RIGHT] = {INPUT_KEY_RIGHT, INPUT_GAMEPAD_DPAD_RIGHT},
	[A_BRAKE_LEFT] = {INPUT_KEY_C, INPUT_GAMEPAD_L_SHOULDER},
	[A_BRAKE_RIGHT] = {INPUT_KEY_V, INPUT_GAMEPAD_R_SHOULDER},
	[A_THRUST] = {INPUT_KEY_X, INPUT_GAMEPAD_A},
	[A_FIRE] = {INPUT_KEY_Z, INPUT_GAMEPAD_X},
	[A_CHANGE_VIEW] = {INPUT_KEY_A, INPUT_GAMEPAD_Y},
};

static const uint8_t p2_buttons[NUM_GAME_ACTIONS][2] = {
	[A_UP] = {INPUT_KEY_I, INPUT_GAMEPAD2_DPAD_UP},
	[A_DOWN] = {INPUT_KEY_K, INPUT_GAMEPAD2_DPAD_DOWN},
	[A_LEFT] = {INPUT_KEY_J, INPUT_GAMEPAD2_DPAD_LEFT},
	[A_RIGHT] = {INPUT_KEY_L, INPUT_GAMEPAD2_DPAD_RIGHT},
	[A_BRAKE_LEFT] = {INPUT_KEY_COMMA, INPUT_GAMEPAD2_L_SHOULDER},
	[A_BRAKE_RIGHT] = {INPUT_KEY_PERIOD, INPUT_GAMEPAD2_R_SHOULDER},
	[A_THRUST] = {INPUT_KEY_N, INPUT_GAMEPAD2_A},
	[A_FIRE] = {INPUT_KEY_B, INPUT_GAMEPAD2_X},
	[A_CHANGE_VIEW] = {INPUT_KEY_O, INPUT_GAMEPAD2_Y},
};

static void bind_like_game(void) {
	input_init();
	input_set_gamepad_swap(false);
	input_set_player_filter(-1, A_P2_UP, A_P2_CHANGE_VIEW);

	static const uint8_t system_binds[][2] = {
		{INPUT_KEY_UP, A_MENU_UP}, {INPUT_KEY_DOWN, A_MENU_DOWN},
		{INPUT_KEY_LEFT, A_MENU_LEFT}, {INPUT_KEY_RIGHT, A_MENU_RIGHT},
		{INPUT_KEY_W, A_MENU_UP}, {INPUT_KEY_S, A_MENU_DOWN},
		{INPUT_KEY_A, A_MENU_LEFT}, {INPUT_KEY_D, A_MENU_RIGHT},
		{INPUT_KEY_K, A_MENU_UP}, {INPUT_KEY_J, A_MENU_DOWN},
		{INPUT_KEY_H, A_MENU_LEFT}, {INPUT_KEY_L, A_MENU_RIGHT},
		{INPUT_KEY_BACKSPACE, A_MENU_BACK}, {INPUT_KEY_C, A_MENU_BACK}, {INPUT_KEY_V, A_MENU_BACK},
		{INPUT_KEY_X, A_MENU_SELECT}, {INPUT_KEY_RETURN, A_MENU_START}, {INPUT_KEY_ESCAPE, A_MENU_QUIT},

		{INPUT_GAMEPAD_DPAD_UP, A_MENU_UP}, {INPUT_GAMEPAD_DPAD_DOWN, A_MENU_DOWN},
		{INPUT_GAMEPAD_DPAD_LEFT, A_MENU_LEFT}, {INPUT_GAMEPAD_DPAD_RIGHT, A_MENU_RIGHT},
		{INPUT_GAMEPAD_L_STICK_UP, A_MENU_UP}, {INPUT_GAMEPAD_L_STICK_DOWN, A_MENU_DOWN},
		{INPUT_GAMEPAD_L_STICK_LEFT, A_MENU_LEFT}, {INPUT_GAMEPAD_L_STICK_RIGHT, A_MENU_RIGHT},
		{INPUT_GAMEPAD_X, A_MENU_BACK}, {INPUT_GAMEPAD_B, A_MENU_BACK},
		{INPUT_GAMEPAD_A, A_MENU_SELECT}, {INPUT_GAMEPAD_START, A_MENU_START},

		{INPUT_KEY_I, A_MENU_UP}, {INPUT_KEY_K, A_MENU_DOWN},
		{INPUT_KEY_J, A_MENU_LEFT}, {INPUT_KEY_L, A_MENU_RIGHT},
		{INPUT_KEY_N, A_MENU_SELECT}, {INPUT_KEY_B, A_MENU_BACK},

		{INPUT_GAMEPAD2_DPAD_UP, A_MENU_UP}, {INPUT_GAMEPAD2_DPAD_DOWN, A_MENU_DOWN},
		{INPUT_GAMEPAD2_DPAD_LEFT, A_MENU_LEFT}, {INPUT_GAMEPAD2_DPAD_RIGHT, A_MENU_RIGHT},
		{INPUT_GAMEPAD2_B, A_MENU_BACK}, {INPUT_GAMEPAD2_A, A_MENU_SELECT},
		{INPUT_GAMEPAD2_START, A_MENU_START},
	};
	for (int i = 0; i < len(system_binds); i++) {
		input_bind(INPUT_LAYER_SYSTEM, system_binds[i][0], system_binds[i][1]);
	}

	for (int action = 0; action < NUM_GAME_ACTIONS; action++) {
		for (int i = 0; i < 2; i++) {
			input_bind(INPUT_LAYER_USER, p1_buttons[action][i], action);
		}
	}
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_L_STICK_UP, A_UP);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_L_STICK_DOWN, A_DOWN);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_L_STICK_LEFT, A_LEFT);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_L_STICK_RIGHT, A_RIGHT);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_R_TRIGGER, A_THRUST);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD_L_TRIGGER, A_FIRE);

	for (int action = 0; action < NUM_GAME_ACTIONS; action++) {
		for (int i = 0; i < 2; i++) {
			input_bind(INPUT_LAYER_USER, p2_buttons[action][i], A_P2_UP + action);
		}
	}
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD2_L_STICK_UP, A_P2_UP);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD2_L_STICK_DOWN, A_P2_DOWN);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD2_L_STICK_LEFT, A_P2_LEFT);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD2_L_STICK_RIGHT, A_P2_RIGHT);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD2_R_TRIGGER, A_P2_THRUST);
	input_bind(INPUT_LAYER_USER, INPUT_GAMEPAD2_L_TRIGGER, A_P2_FIRE);
}


// -----------------------------------------------------------------------------
// Virtual Xbox pads

typedef struct {
	SDL_Joystick *joy;
	SDL_JoystickID id;
} xpad_t;

static xpad_t pad_a, pad_b; // the two physical pads; which slot they get is up to the game

static xpad_t xpad_plug(const char *name) {
	SDL_VirtualJoystickDesc desc;
	SDL_zero(desc);
	desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
	desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
	desc.naxes = SDL_CONTROLLER_AXIS_MAX;
	desc.nbuttons = SDL_CONTROLLER_BUTTON_DPAD_RIGHT + 1;
	desc.vendor_id = 0x045e;  // Microsoft
	desc.product_id = 0x0b13; // Xbox Wireless Controller (Series X|S)
	desc.button_mask = (1 << (SDL_CONTROLLER_BUTTON_DPAD_RIGHT + 1)) - 1;
	desc.axis_mask = (1 << SDL_CONTROLLER_AXIS_MAX) - 1;
	desc.name = name;

	int index = SDL_JoystickAttachVirtualEx(&desc);
	if (index < 0) {
		printf("can't attach a virtual joystick: %s\n", SDL_GetError());
		exit(1);
	}
	xpad_t pad;
	pad.joy = SDL_JoystickOpen(index);
	pad.id = SDL_JoystickInstanceID(pad.joy);
	// Triggers at rest (the full axis range maps to 0..1)
	SDL_JoystickSetVirtualAxis(pad.joy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_JOYSTICK_AXIS_MIN);
	SDL_JoystickSetVirtualAxis(pad.joy, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, SDL_JOYSTICK_AXIS_MIN);
	return pad;
}

static void xpad_unplug(xpad_t *pad) {
	for (int i = 0; i < SDL_NumJoysticks(); i++) {
		if (SDL_JoystickGetDeviceInstanceID(i) == pad->id) {
			SDL_JoystickClose(pad->joy);
			SDL_JoystickDetachVirtual(i);
			pad->joy = NULL;
			return;
		}
	}
}

static void xpad_button(xpad_t *pad, SDL_GameControllerButton button, bool down) {
	SDL_JoystickSetVirtualButton(pad->joy, button, down ? SDL_PRESSED : SDL_RELEASED);
}

static void xpad_axis(xpad_t *pad, SDL_GameControllerAxis axis, float value) {
	int v = (int)(value * 32767);
	if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
		v = SDL_JOYSTICK_AXIS_MIN + (int)(value * 65535); // 0..1 -> full axis range
	}
	SDL_JoystickSetVirtualAxis(pad->joy, axis, (Sint16)clamp(v, SDL_JOYSTICK_AXIS_MIN, SDL_JOYSTICK_AXIS_MAX));
}

// Slot (0 = gamepad 1, 1 = gamepad 2) the game gave this pad, or -1
static int xpad_slot(xpad_t *pad) {
	return platform_gamepad_slot(pad->id);
}

static void key(button_t button, bool down) {
	input_set_button_state(button, down ? 1.0 : 0.0);
}

// One game frame: pump the SDL events, the test looks at the input, then
// input_clear() as at the end of system_update()
static bool pressed[INPUT_ACTION_MAX];
static void frame(void) {
	input_clear();
	platform_pump_events();
	for (int i = 0; i < INPUT_ACTION_MAX; i++) {
		pressed[i] = input_pressed(i);
	}
}

// A few frames, so that SDL delivers everything
static void frames(int n) {
	for (int i = 0; i < n; i++) {
		frame();
	}
}

static bool pressed_during(int n, uint8_t action) {
	bool any = false;
	for (int i = 0; i < n; i++) {
		frame();
		any |= pressed[action];
	}
	return any;
}

static void plug_both(void) {
	pad_a = xpad_plug("Xbox Controller A");
	frames(3);
	pad_b = xpad_plug("Xbox Controller B");
	frames(3);
	bind_like_game();
}

static void unplug_both(void) {
	if (pad_a.joy) xpad_unplug(&pad_a);
	if (pad_b.joy) xpad_unplug(&pad_b);
	frames(3);
}


// -----------------------------------------------------------------------------
// Tests

// Both pads get a slot, every button and axis drives the right player
static void test_two_pads(void) {
	plug_both();
	CHECK(xpad_slot(&pad_a) == 0);
	CHECK(xpad_slot(&pad_b) == 1);

	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_A, true);
	frames(2);
	CHECK_NEAR(input_state(A_THRUST), 1);
	CHECK_NEAR(input_state(A_P2_THRUST), 0);
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_A, false);
	frames(2);
	CHECK_NEAR(input_state(A_THRUST), 0);

	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_A, true);
	frames(2);
	CHECK_NEAR(input_state(A_P2_THRUST), 1);
	CHECK_NEAR(input_state(A_THRUST), 0);
	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_A, false);
	frames(2);
	CHECK_NEAR(input_state(A_P2_THRUST), 0);

	// Triggers are analog
	xpad_axis(&pad_a, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0.5);
	xpad_axis(&pad_b, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1.0);
	frames(2);
	CHECK_NEAR(input_state(A_THRUST), 0.5);
	CHECK_NEAR(input_state(A_P2_FIRE), 1);
	CHECK_NEAR(input_state(A_FIRE), 0);
	CHECK_NEAR(input_state(A_P2_THRUST), 0);
	xpad_axis(&pad_a, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0);
	xpad_axis(&pad_b, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 0);
	frames(2);
	CHECK_NEAR(input_state(A_THRUST), 0);
	CHECK_NEAR(input_state(A_P2_FIRE), 0);

	// Sticks steer their own ship only
	xpad_axis(&pad_b, SDL_CONTROLLER_AXIS_LEFTX, -1);
	frames(2);
	CHECK(input_state(A_P2_LEFT) > 0.9);
	CHECK_NEAR(input_state(A_LEFT), 0);
	xpad_axis(&pad_b, SDL_CONTROLLER_AXIS_LEFTX, 1);
	frames(2);
	CHECK_NEAR(input_state(A_P2_LEFT), 0);
	CHECK(input_state(A_P2_RIGHT) > 0.9);
	xpad_axis(&pad_b, SDL_CONTROLLER_AXIS_LEFTX, 0);
	frames(2);
	CHECK_NEAR(input_state(A_P2_RIGHT), 0);

	// Both pads navigate the menus
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_DPAD_DOWN, true);
	CHECK(pressed_during(2, A_MENU_DOWN));
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_DPAD_DOWN, false);
	frames(2);
	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_DPAD_DOWN, true);
	CHECK(pressed_during(2, A_MENU_DOWN));
	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_DPAD_DOWN, false);
	frames(2);
	unplug_both();
}

// Two player setup: player 1 picks the pilot with A on pad 1, the next page is
// player 2's (pad 1 is ignored there). Player 1 lets go of A on that page.
// Player 2 must still be able to pick with A, and player 1 must not start the
// race with the thrust stuck on (A is player 1's thrust).
static void test_menu_handoff_p1_to_p2(void) {
	plug_both();
	input_set_player_filter(0, A_P2_UP, A_P2_CHANGE_VIEW); // "PLAYER 1 CHOOSE PILOT"
	frames(1);
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_A, true);
	CHECK(pressed_during(2, A_MENU_SELECT));

	input_set_player_filter(1, A_P2_UP, A_P2_CHANGE_VIEW); // "PLAYER 2 SELECT TEAM"
	frames(2);
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_A, false);
	frames(2);

	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_A, true);
	CHECK(pressed_during(2, A_MENU_SELECT));
	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_A, false);
	frames(2);

	// Keyboard N (player 2) selects too
	key(INPUT_KEY_N, true);
	CHECK(input_pressed(A_MENU_SELECT));
	key(INPUT_KEY_N, false);
	frames(1);

	input_set_player_filter(-1, A_P2_UP, A_P2_CHANGE_VIEW); // race_init()
	frames(2);
	CHECK_NEAR(input_state(A_THRUST), 0);

	// Player 1 accelerates with the right trigger and lets go
	xpad_axis(&pad_a, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0.6);
	frames(2);
	CHECK_NEAR(input_state(A_THRUST), 0.6);
	xpad_axis(&pad_a, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0);
	frames(2);
	CHECK_NEAR(input_state(A_THRUST), 0);
	unplug_both();
}

// Player 2 goes back with B to player 1's page and lets go of B there. Player
// 1 must still be able to go back (B, X or backspace).
static void test_menu_handoff_p2_to_p1(void) {
	plug_both();
	input_set_player_filter(1, A_P2_UP, A_P2_CHANGE_VIEW); // "PLAYER 2 SELECT TEAM"
	frames(1);
	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_B, true);
	CHECK(pressed_during(2, A_MENU_BACK));

	input_set_player_filter(0, A_P2_UP, A_P2_CHANGE_VIEW); // "PLAYER 1 CHOOSE PILOT"
	frames(2);
	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_B, false);
	frames(2);

	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_B, true);
	CHECK(pressed_during(2, A_MENU_BACK));
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_B, false);
	frames(2);

	key(INPUT_KEY_BACKSPACE, true);
	CHECK(input_pressed(A_MENU_BACK));
	key(INPUT_KEY_BACKSPACE, false);
	frames(1);
	unplug_both();
}

// SWAP GAMEPADS changed with the d-pad: the d-pad is released after the swap
static void test_swap_while_held(void) {
	plug_both();
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, true);
	CHECK(pressed_during(2, A_MENU_RIGHT));
	input_set_gamepad_swap(true); // what toggle_swap_gamepads() does on that press
	frames(1);
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, false);
	frames(2);

	CHECK_NEAR(input_state(A_MENU_RIGHT), 0);
	CHECK_NEAR(input_state(A_RIGHT), 0);
	CHECK_NEAR(input_state(A_P2_RIGHT), 0);

	key(INPUT_KEY_RIGHT, true);
	CHECK(input_pressed(A_MENU_RIGHT));
	key(INPUT_KEY_RIGHT, false);
	frames(1);

	// Swapped: pad A is player 2 now, pad B player 1
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_A, true);
	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_X, true);
	frames(2);
	CHECK_NEAR(input_state(A_P2_THRUST), 1);
	CHECK_NEAR(input_state(A_THRUST), 0);
	CHECK_NEAR(input_state(A_FIRE), 1);
	CHECK_NEAR(input_state(A_P2_FIRE), 0);
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_A, false);
	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_X, false);
	frames(2);
	CHECK_NEAR(input_state(A_P2_THRUST), 0);
	CHECK_NEAR(input_state(A_FIRE), 0);

	input_set_gamepad_swap(false);
	unplug_both();
}

// Thrust on A and on the right trigger at the same time: letting go of A
// while the trigger is held keeps the trigger's thrust
static void test_button_and_trigger(void) {
	plug_both();
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_A, true);
	frames(2);
	xpad_axis(&pad_a, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0.7);
	frames(2);
	CHECK(input_state(A_THRUST) > 0.6);
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_A, false);
	frames(2);
	CHECK_NEAR(input_state(A_THRUST), 0.7);
	xpad_axis(&pad_a, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0);
	frames(2);
	CHECK_NEAR(input_state(A_THRUST), 0);

	// Same with the d-pad and the stick: the stick back at rest doesn't stop
	// the d-pad turn
	xpad_axis(&pad_b, SDL_CONTROLLER_AXIS_LEFTX, -0.8);
	frames(2);
	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_DPAD_LEFT, true);
	frames(2);
	xpad_axis(&pad_b, SDL_CONTROLLER_AXIS_LEFTX, 0);
	frames(2);
	CHECK_NEAR(input_state(A_P2_LEFT), 1);
	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_DPAD_LEFT, false);
	frames(2);
	CHECK_NEAR(input_state(A_P2_LEFT), 0);
	unplug_both();
}

// A pad unplugged (or switched off, battery...) while held, then back
static void test_unplug_while_held(void) {
	plug_both();
	xpad_axis(&pad_a, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);
	xpad_axis(&pad_a, SDL_CONTROLLER_AXIS_LEFTX, -1);
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_DPAD_DOWN, true);
	frames(2);
	CHECK_NEAR(input_state(A_THRUST), 1);
	CHECK(input_state(A_LEFT) > 0.9);

	xpad_unplug(&pad_a);
	frames(3);
	CHECK_NEAR(input_state(A_THRUST), 0);
	CHECK_NEAR(input_state(A_LEFT), 0);
	CHECK_NEAR(input_state(A_MENU_DOWN), 0);
	CHECK(xpad_slot(&pad_b) == 1);

	key(INPUT_KEY_DOWN, true);
	CHECK(input_pressed(A_MENU_DOWN));
	key(INPUT_KEY_DOWN, false);

	// Back as player 1
	pad_a = xpad_plug("Xbox Controller A");
	frames(3);
	CHECK(xpad_slot(&pad_a) == 0);
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_A, true);
	frames(2);
	CHECK_NEAR(input_state(A_THRUST), 1);
	CHECK_NEAR(input_state(A_P2_THRUST), 0);
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_A, false);
	frames(2);
	unplug_both();
}

// A worn stick that doesn't come back to the center (15% down, typical drift
// on an old Xbox pad) must not move through the menus nor block the d-pad
static void test_stick_drift(void) {
	plug_both();
	xpad_axis(&pad_a, SDL_CONTROLLER_AXIS_LEFTY, 0.15);
	CHECK(!pressed_during(3, A_MENU_DOWN));
	CHECK_NEAR(input_state(A_DOWN), 0);

	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_DPAD_DOWN, true);
	CHECK(pressed_during(2, A_MENU_DOWN));
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_DPAD_DOWN, false);
	frames(2);
	key(INPUT_KEY_DOWN, true);
	CHECK(input_pressed(A_MENU_DOWN));
	key(INPUT_KEY_DOWN, false);
	frames(1);

	// A real push still counts, from the center
	xpad_axis(&pad_a, SDL_CONTROLLER_AXIS_LEFTY, 1);
	CHECK(pressed_during(2, A_MENU_DOWN));
	CHECK(input_state(A_DOWN) > 0.9);
	xpad_axis(&pad_a, SDL_CONTROLLER_AXIS_LEFTY, 0);
	frames(2);
	unplug_both();
}

// The controls menu waits for a button (input_capture). The press of A that
// opened it, released while waiting, isn't taken; the next press is.
static button_t captured;
static void capture_cb(void *user, button_t button, int32_t ascii_char) {
	if (button != INPUT_INVALID && !input_is_stick_direction(button)) {
		captured = button;
		input_capture(NULL, NULL);
	}
}

static void test_capture(void) {
	plug_both();
	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_A, true);
	frames(2);
	captured = INPUT_INVALID;
	input_capture(capture_cb, NULL);
	xpad_button(&pad_b, SDL_CONTROLLER_BUTTON_A, false);
	frames(2);
	CHECK(captured == INPUT_INVALID);

	xpad_axis(&pad_b, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1);
	frames(2);
	CHECK(captured == INPUT_GAMEPAD2_R_TRIGGER);
	CHECK(input_button_gamepad(captured) == 1);
	xpad_axis(&pad_b, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0);
	frames(2);

	// With SWAP GAMEPADS, pad A is gamepad 2: the controls menu gets the
	// buttons as the game will read them
	input_set_gamepad_swap(true);
	captured = INPUT_INVALID;
	input_capture(capture_cb, NULL);
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_A, true);
	frames(2);
	CHECK(captured == INPUT_GAMEPAD2_A);
	xpad_button(&pad_a, SDL_CONTROLLER_BUTTON_A, false);
	frames(2);
	input_set_gamepad_swap(false);
	input_capture(NULL, NULL);
	unplug_both();
}

int main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IONBF, 0);
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
		printf("SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_version v;
	SDL_GetVersion(&v);
	printf("SDL %d.%d.%d, two virtual Xbox pads\n", v.major, v.minor, v.patch);

	run_test("two pads: slots, buttons, triggers, sticks, menus", test_two_pads);
	run_test("split screen menus: player 1 -> player 2 with A held", test_menu_handoff_p1_to_p2);
	run_test("split screen menus: player 2 -> player 1 with B held", test_menu_handoff_p2_to_p1);
	run_test("SWAP GAMEPADS toggled with the d-pad held", test_swap_while_held);
	run_test("thrust on A and the trigger, d-pad and stick", test_button_and_trigger);
	run_test("pad unplugged while held, plugged back", test_unplug_while_held);
	run_test("stick drift", test_stick_drift);
	run_test("controls menu capture", test_capture);

	SDL_Quit();
	printf("\n%d tests, %d failed (%d failed checks)\n", tests_run, tests_failed, checks_failed);
	return tests_failed ? 1 : 0;
}
