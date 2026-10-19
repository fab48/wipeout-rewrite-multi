#include <string.h>

#include "input.h"
#include "utils.h"

static const char *button_names[INPUT_BUTTON_MAX] = {
	NULL,
	NULL,
	NULL,
	NULL,
	[INPUT_KEY_A] = "A",
	[INPUT_KEY_B] = "B",
	[INPUT_KEY_C] = "C",
	[INPUT_KEY_D] = "D",
	[INPUT_KEY_E] = "E",
	[INPUT_KEY_F] = "F",
	[INPUT_KEY_G] = "G",
	[INPUT_KEY_H] = "H",
	[INPUT_KEY_I] = "I",
	[INPUT_KEY_J] = "J",
	[INPUT_KEY_K] = "K",
	[INPUT_KEY_L] = "L",
	[INPUT_KEY_M] = "M",
	[INPUT_KEY_N] = "N",
	[INPUT_KEY_O] = "O",
	[INPUT_KEY_P] = "P",
	[INPUT_KEY_Q] = "Q",
	[INPUT_KEY_R] = "R",
	[INPUT_KEY_S] = "S",
	[INPUT_KEY_T] = "T",
	[INPUT_KEY_U] = "U",
	[INPUT_KEY_V] = "V",
	[INPUT_KEY_W] = "W",
	[INPUT_KEY_X] = "X",
	[INPUT_KEY_Y] = "Y",
	[INPUT_KEY_Z] = "Z",
	[INPUT_KEY_1] = "1",
	[INPUT_KEY_2] = "2",
	[INPUT_KEY_3] = "3",
	[INPUT_KEY_4] = "4",
	[INPUT_KEY_5] = "5",
	[INPUT_KEY_6] = "6",
	[INPUT_KEY_7] = "7",
	[INPUT_KEY_8] = "8",
	[INPUT_KEY_9] = "9",
	[INPUT_KEY_0] = "0",
	[INPUT_KEY_RETURN] = "RETURN",
	[INPUT_KEY_ESCAPE] = "ESCAPE",
	[INPUT_KEY_BACKSPACE] = "BACKSP",
	[INPUT_KEY_TAB] = "TAB",
	[INPUT_KEY_SPACE] = "SPACE",
	[INPUT_KEY_MINUS] = "MINUS",
	[INPUT_KEY_EQUALS] = "EQUALS",
	[INPUT_KEY_LEFTBRACKET] = "LBRACKET",
	[INPUT_KEY_RIGHTBRACKET] = "RBRACKET",
	[INPUT_KEY_BACKSLASH] = "BSLASH",
	[INPUT_KEY_HASH] = "HASH",
	[INPUT_KEY_SEMICOLON] = "SMICOL",
	[INPUT_KEY_APOSTROPHE] = "APO",
	[INPUT_KEY_TILDE] = "TILDE",
	[INPUT_KEY_COMMA] = "COMMA",
	[INPUT_KEY_PERIOD] = "PERIOD",
	[INPUT_KEY_SLASH] = "SLASH",
	[INPUT_KEY_CAPSLOCK] = "CAPS",
	[INPUT_KEY_F1] = "F1",
	[INPUT_KEY_F2] = "F2",
	[INPUT_KEY_F3] = "F3",
	[INPUT_KEY_F4] = "F4",
	[INPUT_KEY_F5] = "F5",
	[INPUT_KEY_F6] = "F6",
	[INPUT_KEY_F7] = "F7",
	[INPUT_KEY_F8] = "F8",
	[INPUT_KEY_F9] = "F9",
	[INPUT_KEY_F10] = "F10",
	[INPUT_KEY_F11] = "F11",
	[INPUT_KEY_F12] = "F12",
	[INPUT_KEY_PRINTSCREEN] = "PRTSC",
	[INPUT_KEY_SCROLLLOCK] = "SCRLK",
	[INPUT_KEY_PAUSE] = "PAUSE",
	[INPUT_KEY_INSERT] = "INSERT",
	[INPUT_KEY_HOME] = "HOME",
	[INPUT_KEY_PAGEUP] = "PG UP",
	[INPUT_KEY_DELETE] = "DELETE",
	[INPUT_KEY_END] = "END",
	[INPUT_KEY_PAGEDOWN] = "PG DOWN",
	[INPUT_KEY_RIGHT] = "RIGHT",
	[INPUT_KEY_LEFT] = "LEFT",
	[INPUT_KEY_DOWN] = "DOWN",
	[INPUT_KEY_UP] = "UP",
	[INPUT_KEY_NUMLOCK] = "NLOCK",
	[INPUT_KEY_KP_DIVIDE] = "KPDIV",
	[INPUT_KEY_KP_MULTIPLY] = "KPMUL",
	[INPUT_KEY_KP_MINUS] = "KPMINUS",
	[INPUT_KEY_KP_PLUS] = "KPPLUS",
	[INPUT_KEY_KP_ENTER] = "KPENTER",
	[INPUT_KEY_KP_1] = "KP1",
	[INPUT_KEY_KP_2] = "KP2",
	[INPUT_KEY_KP_3] = "KP3",
	[INPUT_KEY_KP_4] = "KP4",
	[INPUT_KEY_KP_5] = "KP5",
	[INPUT_KEY_KP_6] = "KP6",
	[INPUT_KEY_KP_7] = "KP7",
	[INPUT_KEY_KP_8] = "KP8",
	[INPUT_KEY_KP_9] = "KP9",
	[INPUT_KEY_KP_0] = "KP0",
	[INPUT_KEY_KP_PERIOD] = "KPPERIOD",

	[INPUT_KEY_LCTRL] = "LCTRL",
	[INPUT_KEY_LSHIFT] = "LSHIFT",
	[INPUT_KEY_LALT] = "LALT",
	[INPUT_KEY_LGUI] = "LGUI",
	[INPUT_KEY_RCTRL] = "RCTRL",
	[INPUT_KEY_RSHIFT] = "RSHIFT",
	[INPUT_KEY_RALT] = "RALT",
	NULL,
	[INPUT_GAMEPAD_A] = "A",
	[INPUT_GAMEPAD_Y] = "Y",
	[INPUT_GAMEPAD_B] = "B",
	[INPUT_GAMEPAD_X] = "X",
	[INPUT_GAMEPAD_L_SHOULDER] = "LSHLDR",
	[INPUT_GAMEPAD_R_SHOULDER] = "RSHLDR",
	[INPUT_GAMEPAD_L_TRIGGER] = "LTRIG",
	[INPUT_GAMEPAD_R_TRIGGER] = "RTRIG",
	[INPUT_GAMEPAD_SELECT] = "SELECT",
	[INPUT_GAMEPAD_START] = "START",
	[INPUT_GAMEPAD_L_STICK_PRESS] = "LSTK",
	[INPUT_GAMEPAD_R_STICK_PRESS] = "RSTK",
	[INPUT_GAMEPAD_DPAD_UP] = "DPUP",
	[INPUT_GAMEPAD_DPAD_DOWN] = "DPDOWN",
	[INPUT_GAMEPAD_DPAD_LEFT] = "DPLEFT",
	[INPUT_GAMEPAD_DPAD_RIGHT] = "DPRIGHT",
	[INPUT_GAMEPAD_HOME] = "HOME",
	[INPUT_GAMEPAD_L_STICK_UP] = "LSTKUP",
	[INPUT_GAMEPAD_L_STICK_DOWN] = "LSTKDOWN",
	[INPUT_GAMEPAD_L_STICK_LEFT] = "LSTKLEFT",
	[INPUT_GAMEPAD_L_STICK_RIGHT] = "LSTKRIGHT",
	[INPUT_GAMEPAD_R_STICK_UP] = "RSTKUP",
	[INPUT_GAMEPAD_R_STICK_DOWN] = "RSTKDOWN",
	[INPUT_GAMEPAD_R_STICK_LEFT] = "RSTKLEFT",
	[INPUT_GAMEPAD_R_STICK_RIGHT] = "RSTKRIGHT",

	// Second gamepad
	[INPUT_GAMEPAD2_A] = "2A",
	[INPUT_GAMEPAD2_Y] = "2Y",
	[INPUT_GAMEPAD2_B] = "2B",
	[INPUT_GAMEPAD2_X] = "2X",
	[INPUT_GAMEPAD2_L_SHOULDER] = "2LSHLDR",
	[INPUT_GAMEPAD2_R_SHOULDER] = "2RSHLDR",
	[INPUT_GAMEPAD2_L_TRIGGER] = "2LTRIG",
	[INPUT_GAMEPAD2_R_TRIGGER] = "2RTRIG",
	[INPUT_GAMEPAD2_SELECT] = "2SELECT",
	[INPUT_GAMEPAD2_START] = "2START",
	[INPUT_GAMEPAD2_L_STICK_PRESS] = "2LSTK",
	[INPUT_GAMEPAD2_R_STICK_PRESS] = "2RSTK",
	[INPUT_GAMEPAD2_DPAD_UP] = "2DPUP",
	[INPUT_GAMEPAD2_DPAD_DOWN] = "2DPDOWN",
	[INPUT_GAMEPAD2_DPAD_LEFT] = "2DPLEFT",
	[INPUT_GAMEPAD2_DPAD_RIGHT] = "2DPRIGHT",
	[INPUT_GAMEPAD2_HOME] = "2HOME",
	[INPUT_GAMEPAD2_L_STICK_UP] = "2LSTKUP",
	[INPUT_GAMEPAD2_L_STICK_DOWN] = "2LSTKDOWN",
	[INPUT_GAMEPAD2_L_STICK_LEFT] = "2LSTKLEFT",
	[INPUT_GAMEPAD2_L_STICK_RIGHT] = "2LSTKRIGHT",
	[INPUT_GAMEPAD2_R_STICK_UP] = "2RSTKUP",
	[INPUT_GAMEPAD2_R_STICK_DOWN] = "2RSTKDOWN",
	[INPUT_GAMEPAD2_R_STICK_LEFT] = "2RSTKLEFT",
	[INPUT_GAMEPAD2_R_STICK_RIGHT] = "2RSTKRIGHT",
	[INPUT_MOUSE_LEFT] = "MLEFT",
	[INPUT_MOUSE_MIDDLE] = "MMIDDLE",
	[INPUT_MOUSE_RIGHT] = "MRIGHT",
	[INPUT_MOUSE_WHEEL_UP] = "MWUP",
	[INPUT_MOUSE_WHEEL_DOWN] = "MWDOWN",
};

static float actions_state[INPUT_ACTION_MAX];
static bool actions_pressed[INPUT_ACTION_MAX];
static bool actions_released[INPUT_ACTION_MAX];

// Every button as the platform reports it, before the gamepad swap. The state
// of an action is worked out from all the buttons bound to it (the highest
// one wins), so it can't get stuck. It used to be held by the one button that
// pressed it: when the release of that button got lost (ignored by the player
// filter, or coming from the other gamepad after SWAP GAMEPADS), the action
// stayed on and no other button could press or release it any more, until
// the controls were rebound or the game restarted.
static float buttons_state[INPUT_BUTTON_MAX];
static uint8_t bindings[INPUT_LAYER_MAX][INPUT_BUTTON_MAX];

static input_capture_callback_t capture_callback;
static void *capture_user;

static int32_t mouse_x;
static int32_t mouse_y;

static bool gamepad_swap = false;
static int player_filter = -1;
static uint8_t player2_action_min = 0, player2_action_max = 0;

void input_init(void) {
	clear(buttons_state);
	input_unbind_all(INPUT_LAYER_SYSTEM);
	input_unbind_all(INPUT_LAYER_USER);
}

void input_cleanup(void) {

}

void input_clear(void) {
	clear(actions_pressed);
	clear(actions_released);
}

int input_button_gamepad(button_t button) {
	if (button >= INPUT_GAMEPAD_A && button <= INPUT_GAMEPAD_R_STICK_RIGHT) {
		return 0;
	}
	if (button >= INPUT_GAMEPAD2_A && button <= INPUT_GAMEPAD2_R_STICK_RIGHT) {
		return 1;
	}
	return -1;
}

// Gamepad 1 <-> gamepad 2; the same in both directions
static button_t input_swap_gamepads(button_t button) {
	if (gamepad_swap) {
		if (input_button_gamepad(button) == 0) {
			return button + INPUT_GAMEPAD2_OFFSET;
		}
		else if (input_button_gamepad(button) == 1) {
			return button - INPUT_GAMEPAD2_OFFSET;
		}
	}
	return button;
}

static bool input_button_belongs_to_player2(button_t button) {
	if (button >= INPUT_GAMEPAD2_A && button <= INPUT_GAMEPAD2_R_STICK_RIGHT) {
		return true;
	}
	if (button < INPUT_KEY_MAX) {
		uint8_t action = bindings[INPUT_LAYER_USER][button];
		return action >= player2_action_min && action <= player2_action_max;
	}
	return false;
}

// Menus for one player only (team/pilot selection in split screen): the
// other player's gamepad is ignored. The keyboard is shared and always
// goes through, or a player without a gamepad could get stuck.
static bool input_button_filtered(button_t button) {
	if (player_filter < 0) {
		return false;
	}
	bool is_gamepad1 = button >= INPUT_GAMEPAD_A && button <= INPUT_GAMEPAD_R_STICK_RIGHT;
	bool is_gamepad2 = input_button_belongs_to_player2(button) && button >= INPUT_GAMEPAD2_A;
	return (player_filter == 0 && is_gamepad2) || (player_filter == 1 && is_gamepad1);
}

// State of a button as the game sees it (after the gamepad swap, the player
// filter and the dead zone)
static float input_button_state(button_t button) {
	if (input_button_filtered(button)) {
		return 0;
	}
	float state = buttons_state[input_swap_gamepads(button)];

	// Worn sticks don't come back to the center: 10% was not enough, a stick
	// resting a bit off held a menu direction (the d-pad and the keyboard
	// could no longer press it) and steered a little. Rescaled, so that the
	// stick still starts from 0 out of the dead zone.
	if (input_is_stick_direction(button)) {
		return state > INPUT_DEADZONE_STICK
			? (state - INPUT_DEADZONE_STICK) / (1.0 - INPUT_DEADZONE_STICK)
			: 0;
	}
	return state > INPUT_DEADZONE ? state : 0;
}

// Work out the state of an action from its buttons. may_press is false when
// the state changes without a button being pressed (filter, swap, binding):
// a button that was already held then has to be pressed again.
static void input_update_action(uint8_t action, bool may_press) {
	if (action == INPUT_ACTION_NONE) {
		return;
	}

	float state = 0;
	for (int layer = 0; layer < INPUT_LAYER_MAX; layer++) {
		for (int button = 0; button < INPUT_BUTTON_MAX; button++) {
			if (bindings[layer][button] == action) {
				state = max(state, input_button_state(button));
			}
		}
	}

	if (state && !actions_state[action] && may_press) {
		actions_pressed[action] = true;
	}
	else if (!state && actions_state[action]) {
		actions_released[action] = true;
	}
	actions_state[action] = state;
}

static void input_update_all_actions(void) {
	for (int action = 0; action < INPUT_ACTION_MAX; action++) {
		input_update_action(action, false);
	}
}

void input_set_gamepad_swap(bool swap) {
	if (swap != gamepad_swap) {
		gamepad_swap = swap;
		input_update_all_actions();
	}
}

void input_set_player_filter(int player, uint8_t p2_action_min, uint8_t p2_action_max) {
	player2_action_min = p2_action_min;
	player2_action_max = p2_action_max;
	if (player != player_filter) {
		// What the device we start ignoring holds is released
		player_filter = player;
		input_update_all_actions();
	}
}

void input_set_button_state(button_t button, float state) {
	error_if(button < 0 || button >= INPUT_BUTTON_MAX, "Invalid input button %d", button);

	float previous = buttons_state[button];
	buttons_state[button] = state;

	button_t game_button = input_swap_gamepads(button);
	input_update_action(bindings[INPUT_LAYER_SYSTEM][game_button], true);
	input_update_action(bindings[INPUT_LAYER_USER][game_button], true);

	// The capture gets everything, the player filter doesn't apply; only new
	// presses, not a trigger or a key that was already held
	if (capture_callback) {
		if (state > INPUT_DEADZONE_CAPTURE && previous <= INPUT_DEADZONE_CAPTURE) {
			capture_callback(capture_user, game_button, 0);
		}
	}
}

void input_set_mouse_pos(int32_t x, int32_t y) {
	mouse_x = x;
	mouse_y = y;
}

void input_capture(input_capture_callback_t cb, void *user) {
	capture_callback = cb;
	capture_user = user;
	input_clear();
}

bool input_capture_active(void) {
	return capture_callback != NULL;
}

void input_textinput(int32_t ascii_char) {
	if (capture_callback) {
		capture_callback(capture_user, INPUT_INVALID, ascii_char);
	}
}

void input_bind(input_layer_t layer, button_t button, uint8_t action) {
	error_if(button < 0 || button >= INPUT_BUTTON_MAX, "Invalid input button %d", button);
	error_if(action < 0 || action >= INPUT_ACTION_MAX, "Invalid input action %d", action);
	error_if(layer < 0 || layer >= INPUT_LAYER_MAX, "Invalid input layer %d", layer);

	uint8_t previous = bindings[layer][button];
	bindings[layer][button] = action;
	input_update_action(previous, false);
	input_update_action(action, false);
}

bool input_is_stick_direction(button_t button) {
	return
		(button >= INPUT_GAMEPAD_L_STICK_UP && button <= INPUT_GAMEPAD_R_STICK_RIGHT) ||
		(button >= INPUT_GAMEPAD2_L_STICK_UP && button <= INPUT_GAMEPAD2_R_STICK_RIGHT);
}

uint8_t input_bound_to_action(button_t button) {
	error_if(button < 0 || button >= INPUT_BUTTON_MAX, "Invalid input button %d", button);
	return bindings[INPUT_LAYER_USER][button];
}

void input_unbind(input_layer_t layer, button_t button) {
	error_if(layer < 0 || layer >= INPUT_LAYER_MAX, "Invalid input layer %d", layer);
	error_if(button < 0 || button >= INPUT_BUTTON_MAX, "Invalid input button %d", button);

	uint8_t previous = bindings[layer][button];
	bindings[layer][button] = INPUT_ACTION_NONE;
	input_update_action(previous, false);
}

void input_unbind_all(input_layer_t layer) {
	error_if(layer < 0 || layer >= INPUT_LAYER_MAX, "Invalid input layer %d", layer);
	
	for (uint32_t button = 0; button < INPUT_BUTTON_MAX; button++) {
		input_unbind(layer, button);
	}
}


float input_state(uint8_t action) {
	error_if(action < 0 || action >= INPUT_ACTION_MAX, "Invalid input action %d", action);
	return actions_state[action];
}


bool input_pressed(uint8_t action) {
	error_if(action < 0 || action >= INPUT_ACTION_MAX, "Invalid input action %d", action);
	return actions_pressed[action];
}


bool input_released(uint8_t action) {
	error_if(action < 0 || action >= INPUT_ACTION_MAX, "Invalid input action %d", action);
	return actions_released[action];
}

vec2_t input_mouse_pos(void) {
	return vec2(mouse_x, mouse_y);
}


button_t input_name_to_button(const char *name) {
	for (int32_t i = 0; i < INPUT_BUTTON_MAX; i++) {
		if (button_names[i] && strcmp(name, button_names[i]) == 0) {
			return i;
		}
	}
	return INPUT_INVALID;
}

const char *input_button_to_name(button_t button) {
	if (
		button < 0 || button >= INPUT_BUTTON_MAX ||
		!button_names[button]
	) {
		return NULL;
	}
	return button_names[button];
}


#if defined(__EMSCRIPTEN__)
	#include <emscripten/emscripten.h>
	void EMSCRIPTEN_KEEPALIVE set_button(uint32_t button, uint32_t state) {
		input_set_button_state(button, state ? 1.0 : 0.0);
	}
#endif
