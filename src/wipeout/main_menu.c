#include "../utils.h"
#include "../system.h"
#include "../mem.h"
#include "../platform.h"
#include "../input.h"

#include "menu.h"
#include "main_menu.h"
#include "game.h"
#include "image.h"
#include "ui.h"

static void page_main_init(menu_t *menu);
static void page_options_init(menu_t *menu);
static void page_race_class_init(menu_t *menu);
static void page_race_type_init(menu_t *menu);
static void page_team_init(menu_t *menu);
static void page_pilot_init(menu_t *menu);
static void page_circuit_init(menu_t *menu);
static void page_options_controls_init(menu_t *menu);
static void page_options_video_init(menu_t *menu);
static void page_options_audio_init(menu_t *menu);
static void page_options_highscores_init(menu_t *menu);

static uint16_t background;
static texture_list_t track_images;
static menu_t *main_menu;

static struct {
	Object *race_classes[2];
	Object *teams[4];
	Object *pilots[8];
	struct { Object *stopwatch, *save, *load, *headphones, *cd; } options;
	struct { Object *championship, *msdos, *single_race, *options; } misc;
	Object *rescue;
	Object *controller;
} models;

static void draw_model(Object *model, vec2_t offset, vec3_t pos, float rotation) {
	render_set_view(vec3(0,0,0), vec3(0, -M_PI, -M_PI));
	render_set_screen_position(offset);
	mat4_t mat = mat4_identity();
	mat4_set_translation(&mat, pos);
	mat4_set_yaw_pitch_roll(&mat, vec3(0, rotation, M_PI));
	object_draw(model, &mat);
	render_set_screen_position(vec2(0, 0));
}

// -----------------------------------------------------------------------------
// Main Menu

static void button_start_game(menu_t *menu, int data) {
	page_race_class_init(menu);
}

static void button_options(menu_t *menu, int data) {
	page_options_init(menu);
}

static void button_quit_confirm(menu_t *menu, int data) {
	if (data) {
		system_exit();
	}
	else {
		menu_pop(menu);
	}
}

static void button_quit(menu_t *menu, int data) {
	menu_confirm(menu, "ARE YOU SURE YOU", "WANT TO QUIT", "YES", "NO", button_quit_confirm);
}

static void page_main_draw(menu_t *menu, int data) {
	switch (data) {
		case 0: draw_model(g.ships[0].model, vec2(0, -0.1), vec3(0, 0, -700), system_cycle_time()); break;
		case 1: draw_model(models.misc.options, vec2(0, -0.2), vec3(0, 0, -700), system_cycle_time()); break;
		case 2: draw_model(models.misc.msdos, vec2(0, -0.2), vec3(0, 0, -700), system_cycle_time()); break;
	}
}

static void page_main_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "OPTIONS", page_main_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;

	menu_page_add_button(page, 0, "START GAME", button_start_game);
	menu_page_add_button(page, 1, "OPTIONS", button_options);

	#ifndef __EMSCRIPTEN__
		menu_page_add_button(page, 2, "QUIT", button_quit);
	#endif
}



// -----------------------------------------------------------------------------
// Options

static void button_controls(menu_t *menu, int data) {
	page_options_controls_init(menu);
}

static void button_video(menu_t *menu, int data) {
	page_options_video_init(menu);
}

static void button_audio(menu_t *menu, int data) {
	page_options_audio_init(menu);
}

static void button_highscores(menu_t *menu, int data) {
	page_options_highscores_init(menu);
}

static void page_options_draw(menu_t *menu, int data) {
	switch (data) {
		case 0: draw_model(models.controller, vec2(0, -0.1), vec3(0, 0, -6000), system_cycle_time()); break;
		case 1: draw_model(models.rescue, vec2(0, -0.2), vec3(0, 0, -700), system_cycle_time()); break; // TODO: needs better model
		case 2: draw_model(models.options.headphones, vec2(0, -0.2), vec3(0, 0, -300), system_cycle_time()); break;
		case 3: draw_model(models.options.stopwatch, vec2(0, -0.2), vec3(0, 0, -400), system_cycle_time()); break;
	}
}

static void page_options_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "OPTIONS", page_options_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;
	menu_page_add_button(page, 0, "CONTROLS", button_controls);
	menu_page_add_button(page, 1, "VIDEO", button_video);
	menu_page_add_button(page, 2, "AUDIO", button_audio);
	menu_page_add_button(page, 3, "BEST TIMES", button_highscores);
}


// -----------------------------------------------------------------------------
// Options Controls

static const char *button_names[NUM_GAME_ACTIONS][2] = {};
static int control_current_action;
static int control_current_player = 0;
static char *controls_player2_title = "PLAYER 2 CONTROLS";
static float await_input_deadline;

// The button table that is being edited
static uint8_t (*control_buttons(void))[2] {
	return control_current_player == 1 ? save2.buttons : save.buttons;
}

static void control_set_dirty(void) {
	if (control_current_player == 1) {
		save2.is_dirty = true;
	}
	else {
		save.is_dirty = true;
	}
}

void button_capture(void *user, button_t button, int32_t ascii_char) {
	if (button == INPUT_INVALID) {
		return;
	}

	menu_t *menu = (menu_t *)user;
	if (button == INPUT_KEY_ESCAPE) {
		input_capture(NULL, NULL);
		menu_pop(menu);
		return;
	}

	int index = button < INPUT_KEY_MAX ? 0 : 1; // joypad or keyboard
	uint8_t (*buttons)[2] = control_buttons();
	int action = control_current_player == 1 ? A_P2_UP + control_current_action : control_current_action;

	// unbind this button if it's bound anywhere (for either player)
	for (int i = 0; i < NUM_GAME_ACTIONS; i++) {
		if (save.buttons[i][index] == button) {
			save.buttons[i][index] = INPUT_INVALID;
			save.is_dirty = true;
		}
		if (save2.buttons[i][index] == button) {
			save2.buttons[i][index] = INPUT_INVALID;
			save2.is_dirty = true;
		}
	}
	input_unbind(INPUT_LAYER_USER, button);

	// unbind the button previously used for this action
	if (buttons[control_current_action][index] != INPUT_INVALID) {
		input_unbind(INPUT_LAYER_USER, buttons[control_current_action][index]);
	}

	input_capture(NULL, NULL);
	input_bind(INPUT_LAYER_USER, button, action);
	buttons[control_current_action][index] = button;
	control_set_dirty();
	menu_pop(menu);
}

static void page_options_control_set_draw(menu_t *menu, int data) {
	float remaining = await_input_deadline - platform_now();

	menu_page_t *page = &menu->pages[menu->index];
	char remaining_text[2] = { '0' + (uint8_t)clamp(remaining + 1, 0, 3), '\0'};
	vec2i_t pos = vec2i(page->items_pos.x, page->items_pos.y + 24);
	ui_draw_text_centered(remaining_text, ui_scaled_pos(page->items_anchor, pos), UI_SIZE_16, UI_COLOR_DEFAULT);

	if (remaining <= 0) {
		input_capture(NULL, NULL);
		menu_pop(menu);
		return;
	}
}

static void page_options_controls_set_init(menu_t *menu, int data) {
	control_current_action = data;
	await_input_deadline = platform_now() + 3;

	menu_page_t *page = menu_push(menu, "AWAITING INPUT", page_options_control_set_draw);
	input_capture(button_capture, menu);
}


static void page_options_control_draw(menu_t *menu, int data) {
	menu_page_t *page = &menu->pages[menu->index];

	// Both the player 1 and the player 2 page use this draw function
	control_current_player = (page->title == controls_player2_title) ? 1 : 0;

	int left = page->items_pos.x + page->block_width - 100;
	int right = page->items_pos.x + page->block_width;
	int line_y = page->items_pos.y - 20;

	vec2i_t left_head_pos = vec2i(left - ui_text_width("KEYBOARD", UI_SIZE_8), line_y);
	ui_draw_text("KEYBOARD", ui_scaled_pos(page->items_anchor, left_head_pos), UI_SIZE_8, UI_COLOR_DEFAULT);

	vec2i_t right_head_pos = vec2i(right - ui_text_width("JOYSTICK", UI_SIZE_8), line_y);
	ui_draw_text("JOYSTICK", ui_scaled_pos(page->items_anchor, right_head_pos), UI_SIZE_8, UI_COLOR_DEFAULT);
	line_y += 20;

	uint8_t (*buttons)[2] = control_buttons();
	for (int action = 0; action < NUM_GAME_ACTIONS; action++) {
		rgba_t text_color = UI_COLOR_DEFAULT;
		if (action == page->index) {
			text_color = UI_COLOR_ACCENT;
		}

		if (buttons[action][0] != INPUT_INVALID) {
			const char *name = input_button_to_name(buttons[action][0]);
			if (!name) {
				name = "UNKNWN";
			}
			vec2i_t pos = vec2i(left - ui_text_width(name, UI_SIZE_8), line_y);
			ui_draw_text(name, ui_scaled_pos(page->items_anchor, pos), UI_SIZE_8, text_color);
		}
		if (buttons[action][1] != INPUT_INVALID) {
			const char *name = input_button_to_name(buttons[action][1]);
			if (!name) {
				name = "UNKNWN";
			}
			vec2i_t pos = vec2i(right - ui_text_width(name, UI_SIZE_8), line_y);
			ui_draw_text(name, ui_scaled_pos(page->items_anchor, pos), UI_SIZE_8, text_color);
		}
		line_y += 12;
	}
}

static void toggle_analog_response(menu_t *menu, int data) {
	save.analog_response = (float)data + 1;
	save.is_dirty = true;
}

static const char *analog_response[] = {"LINEAR", "MODERATE", "HEAVY"};

static void page_options_controls_init_for_player(menu_t *menu, int player);

static void button_player2_controls(menu_t *menu, int data) {
	page_options_controls_init_for_player(menu, 1);
}

static void page_options_controls_init(menu_t *menu) {
	page_options_controls_init_for_player(menu, 0);
}

static void page_options_controls_init_for_player(menu_t *menu, int player) {
	control_current_player = player;
	menu_page_t *page = menu_push(menu, player == 1 ? controls_player2_title : "CONTROLS", page_options_control_draw);
	flags_set(page->layout_flags, MENU_VERTICAL | MENU_FIXED);
	page->title_pos = vec2i(-160, -100);
	page->title_anchor = UI_POS_MIDDLE | UI_POS_CENTER;
	page->items_pos = vec2i(-160, -50);
	page->block_width = 320;
	page->items_anchor = UI_POS_MIDDLE | UI_POS_CENTER;

	// const char *thrust_name = button_name(A_THRUST);
	// printf("thrust: %s\n", thrust_name);
	menu_page_add_button(page, A_UP, "UP", page_options_controls_set_init);
	menu_page_add_button(page, A_DOWN, "DOWN", page_options_controls_set_init);
	menu_page_add_button(page, A_LEFT, "LEFT", page_options_controls_set_init);
	menu_page_add_button(page, A_RIGHT, "RIGHT", page_options_controls_set_init);
	menu_page_add_button(page, A_BRAKE_LEFT, "BRAKE L", page_options_controls_set_init);
	menu_page_add_button(page, A_BRAKE_RIGHT, "BRAKE R", page_options_controls_set_init);
	menu_page_add_button(page, A_THRUST, "THRUST", page_options_controls_set_init);
	menu_page_add_button(page, A_FIRE, "FIRE", page_options_controls_set_init);
	menu_page_add_button(page, A_CHANGE_VIEW, "VIEW", page_options_controls_set_init);

	if (player == 0) {
		menu_page_add_toggle(page, save.analog_response - 1, "ANALOG RESPONSE", analog_response, len(analog_response), toggle_analog_response);
		menu_page_add_button(page, 0, "PLAYER 2 CONTROLS", button_player2_controls);
	}
}

// -----------------------------------------------------------------------------
// Options Video

static void toggle_fullscreen(menu_t *menu, int data) {
	save.fullscreen = data;
	save.is_dirty = true;
	platform_set_fullscreen(save.fullscreen);
}

static void toggle_internal_roll(menu_t *menu, int data) {
	save.internal_roll = (float)data * 0.1;
	save.is_dirty = true;
}

static void toggle_draw_stats(menu_t *menu, int data) {
	save.draw_stats = data;
	save.is_dirty = true;
}

static void toggle_ui_scale(menu_t *menu, int data) {
	save.ui_scale = data;
	save.is_dirty = true;
}

static void toggle_res(menu_t *menu, int data) {
	render_set_resolution(data);
	save.screen_res = data;
	save.is_dirty = true;
}

static void toggle_post_flag(int flag, int enabled) {
	if (enabled) {
		save.post_effect |= flag;
	}
	else {
		save.post_effect &= ~flag;
	}
	render_set_post_effect(save.post_effect);
	save.is_dirty = true;
}

static void toggle_post_crt(menu_t *menu, int data) {
	toggle_post_flag(RENDER_POST_CRT, data);
}

static void toggle_post_bloom(menu_t *menu, int data) {
	toggle_post_flag(RENDER_POST_BLOOM, data);
}

static void toggle_post_motion_blur(menu_t *menu, int data) {
	toggle_post_flag(RENDER_POST_MOTION_BLUR, data);
}

static void toggle_post_lighting(menu_t *menu, int data) {
	toggle_post_flag(RENDER_POST_LIGHTING, data);
}

static void toggle_screen_shake(menu_t *menu, int data) {
	save.screen_shake = (float)data * 0.5;
	save.is_dirty = true;
}

static const char *opts_off_on[] = {"OFF", "ON"};
static const char *opts_roll[] = {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"};
static const char *opts_ui_sizes[] = {"AUTO", "1X", "2X", "3X", "4X"};
static const char *opts_draw_stats[] = {"OFF", "FPS", "DEBUG"};
static const char *opts_res[] = {"NATIVE", "240P", "480P", "720P"};
static const char *opts_screen_shake[] = {"DISABLED", "REDUCED", "FULL"};

static void page_options_video_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "VIDEO OPTIONS", NULL);
	flags_set(page->layout_flags, MENU_VERTICAL | MENU_FIXED);
	page->title_pos = vec2i(-160, -100);
	page->title_anchor = UI_POS_MIDDLE | UI_POS_CENTER;
	page->items_pos = vec2i(-160, -60);
	page->block_width = 320;
	page->items_anchor = UI_POS_MIDDLE | UI_POS_CENTER;

	#ifndef __EMSCRIPTEN__
		menu_page_add_toggle(page, save.fullscreen, "FULLSCREEN", opts_off_on, len(opts_off_on), toggle_fullscreen);
	#endif
	menu_page_add_toggle(page, save.internal_roll * 10, "INTERNAL VIEW ROLL", opts_roll, len(opts_roll), toggle_internal_roll);
	menu_page_add_toggle(page, save.screen_shake * 2, "SCREEN SHAKE", opts_screen_shake, len(opts_screen_shake), toggle_screen_shake);
	menu_page_add_toggle(page, save.ui_scale, "UI SCALE", opts_ui_sizes, len(opts_ui_sizes), toggle_ui_scale);
	menu_page_add_toggle(page, save.draw_stats, "DRAW STATS", opts_draw_stats, len(opts_draw_stats), toggle_draw_stats);
	menu_page_add_toggle(page, save.screen_res, "SCREEN RESOLUTION", opts_res, len(opts_res), toggle_res);
	menu_page_add_toggle(page, (save.post_effect & RENDER_POST_CRT) ? 1 : 0, "CRT EFFECT", opts_off_on, len(opts_off_on), toggle_post_crt);
	menu_page_add_toggle(page, (save.post_effect & RENDER_POST_BLOOM) ? 1 : 0, "BLOOM", opts_off_on, len(opts_off_on), toggle_post_bloom);
	menu_page_add_toggle(page, (save.post_effect & RENDER_POST_MOTION_BLUR) ? 1 : 0, "MOTION BLUR", opts_off_on, len(opts_off_on), toggle_post_motion_blur);
	menu_page_add_toggle(page, (save.post_effect & RENDER_POST_LIGHTING) ? 1 : 0, "PBR LIGHTING", opts_off_on, len(opts_off_on), toggle_post_lighting);
}

// -----------------------------------------------------------------------------
// Options Audio

static void toggle_music_volume(menu_t *menu, int data) {
	save.music_volume = (float)data * 0.1;
	save.is_dirty = true;
}

static void toggle_sfx_volume(menu_t *menu, int data) {
	save.sfx_volume = (float)data * 0.1;	
	save.is_dirty = true;
}

static const char *opts_volume[] = {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"};

static void page_options_audio_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "AUDIO OPTIONS", NULL);

	flags_set(page->layout_flags, MENU_VERTICAL | MENU_FIXED);
	page->title_pos = vec2i(-160, -100);
	page->title_anchor = UI_POS_MIDDLE | UI_POS_CENTER;
	page->items_pos = vec2i(-160, -80);
	page->block_width = 320;
	page->items_anchor = UI_POS_MIDDLE | UI_POS_CENTER;

	menu_page_add_toggle(page, save.music_volume * 10, "MUSIC VOLUME", opts_volume, len(opts_volume), toggle_music_volume);
	menu_page_add_toggle(page, save.sfx_volume * 10, "SOUND EFFECTS VOLUME", opts_volume, len(opts_volume), toggle_sfx_volume);
}

// -----------------------------------------------------------------------------
// Options Best Times

static int options_highscores_race_class;
static int options_highscores_circuit;
static int options_highscores_tab;

static void page_options_highscores_viewer_input_handler() {
	int last_race_class_index = options_highscores_race_class;
	int last_circuit_index = options_highscores_circuit;

	if (input_pressed(A_MENU_UP)) {
		options_highscores_race_class--;
	}
	else if (input_pressed(A_MENU_DOWN)) {
		options_highscores_race_class++;
	}
	options_highscores_race_class = wrap_around(options_highscores_race_class, 0, NUM_RACE_CLASSES);

	if (input_pressed(A_MENU_LEFT)) {
		do {
			options_highscores_circuit = wrap_around(options_highscores_circuit - 1, 0, NUM_CIRCUITS);
		} while (!g.installed_circuits[options_highscores_circuit]);
	}
	else if (input_pressed(A_MENU_RIGHT)) {
		do {
			options_highscores_circuit = wrap_around(options_highscores_circuit + 1, 0, NUM_CIRCUITS);
		} while (!g.installed_circuits[options_highscores_circuit]);
	}

	if ((last_race_class_index != options_highscores_race_class) ||
		(last_circuit_index != options_highscores_circuit)) {
		sfx_play(SFX_MENU_MOVE);
	}
}

static void page_options_highscores_viewer_draw(menu_t *menu, int data) {
	ui_pos_t anchor = UI_POS_MIDDLE | UI_POS_CENTER;

	vec2i_t pos = vec2i(0, -70);
	ui_draw_text_centered(def.race_classes[options_highscores_race_class].name, ui_scaled_pos(anchor, pos), UI_SIZE_12, UI_COLOR_DEFAULT);
	pos.y += 16;
	ui_draw_text_centered(def.circuits[options_highscores_circuit].name, ui_scaled_pos(anchor, pos), UI_SIZE_12, UI_COLOR_ACCENT);
	
	vec2i_t entry_pos = vec2i(pos.x - 110, pos.y + 24);
	highscores_t *hs = &save.highscores[options_highscores_race_class][options_highscores_circuit][options_highscores_tab];
	for (int i = 0; i < NUM_HIGHSCORES; i++) {
		ui_draw_text(hs->entries[i].name, ui_scaled_pos(anchor, entry_pos), UI_SIZE_16, UI_COLOR_DEFAULT);
		ui_draw_time(hs->entries[i].time, ui_scaled_pos(anchor, vec2i(entry_pos.x + 110, entry_pos.y)), UI_SIZE_16, UI_COLOR_DEFAULT);
		entry_pos.y += 24;
	}

	vec2i_t lap_pos = vec2i(entry_pos.x - 40, entry_pos.y + 8);
	ui_draw_text("LAP RECORD", ui_scaled_pos(anchor, lap_pos), UI_SIZE_12, UI_COLOR_ACCENT);
	ui_draw_time(hs->lap_record, ui_scaled_pos(anchor, vec2i(lap_pos.x + 180, lap_pos.y - 4)), UI_SIZE_16, UI_COLOR_DEFAULT);

	page_options_highscores_viewer_input_handler();
}

static void page_options_highscores_viewer_init(menu_t *menu) {
	menu_page_t *page;
	if (options_highscores_tab == HIGHSCORE_TAB_TIME_TRIAL) {
		page = menu_push(menu, "BEST TIME TRIAL TIMES", page_options_highscores_viewer_draw);
	}
	else /*options_highscores_tab == HIGHSCORE_TAB_RACE)*/ {
		page = menu_push(menu, "BEST RACE TIMES", page_options_highscores_viewer_draw);
	}

	flags_add(page->layout_flags, MENU_FIXED);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->title_pos = vec2i(0, 30);
}

static void button_highscores_viewer(menu_t *menu, int data) {
	options_highscores_tab = data;
	page_options_highscores_viewer_init(menu);
}

static void page_options_highscores_draw(menu_t *menu, int data) {
	draw_model(models.options.stopwatch, vec2(0, -0.2), vec3(0, 0, -400), system_cycle_time());
}

static void page_options_highscores_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "VIEW BEST TIMES", page_options_highscores_draw);

	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;

	options_highscores_race_class = RACE_CLASS_VENOM;
	options_highscores_circuit = CIRCUIT_ALTIMA_VII;

	menu_page_add_button(page, HIGHSCORE_TAB_TIME_TRIAL, "TIME TRIAL TIMES", button_highscores_viewer);
	menu_page_add_button(page, HIGHSCORE_TAB_RACE, "RACE TIMES", button_highscores_viewer);
}



// -----------------------------------------------------------------------------
// Racing class

static void button_race_class_select(menu_t *menu, int data) {
	if (!save.has_rapier_class && data == RACE_CLASS_RAPIER) {
		return;
	}
	g.race_class = data;
	page_race_type_init(menu);
}

static void page_race_class_draw(menu_t *menu, int data) {
	menu_page_t *page = &menu->pages[menu->index];
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;
	draw_model(models.race_classes[data], vec2(0, -0.2), vec3(0, 0, -350), system_cycle_time());

	if (!save.has_rapier_class && data == RACE_CLASS_RAPIER) {
		render_set_view_2d();
		vec2i_t pos = vec2i(page->items_pos.x, page->items_pos.y + 32);
		ui_draw_text_centered("NOT AVAILABLE", ui_scaled_pos(page->items_anchor, pos), UI_SIZE_12, UI_COLOR_ACCENT);
	}
}

static void page_race_class_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "SELECT RACING CLASS", page_race_class_draw);
	for (int i = 0; i < len(def.race_classes); i++) {
		menu_page_add_button(page, i, def.race_classes[i].name, button_race_class_select);
	}
}



// -----------------------------------------------------------------------------
// Race Type

// Pseudo race type for the menu; a single race with two players
#define MENU_RACE_TYPE_TWO_PLAYER NUM_RACE_TYPES

// The team and pilot pages are used for both players. The player is encoded
// in the button data.
#define MENU_PLAYER_DATA(PLAYER, VALUE) ((PLAYER) * 100 + (VALUE))
#define MENU_DATA_PLAYER(DATA) ((DATA) / 100)
#define MENU_DATA_VALUE(DATA) ((DATA) % 100)

static void page_team_init_for_player(menu_t *menu, int player);
static void page_pilot_init_for_player(menu_t *menu, int player);

static void button_race_type_select(menu_t *menu, int data) {
	g.num_players = 1;
	if (data == MENU_RACE_TYPE_TWO_PLAYER) {
		g.num_players = 2;
		data = RACE_TYPE_SINGLE;
	}
	g.race_type = data;
	g.highscore_tab = g.race_type == RACE_TYPE_TIME_TRIAL ? HIGHSCORE_TAB_TIME_TRIAL : HIGHSCORE_TAB_RACE;
	page_team_init(menu);
}

static void page_race_type_draw(menu_t *menu, int data) {
	switch (data) {
		case 0: draw_model(models.misc.championship, vec2(0, -0.2), vec3(0, 0, -400), system_cycle_time()); break;
		case 1: draw_model(models.misc.single_race, vec2(0, -0.2), vec3(0, 0, -400), system_cycle_time()); break;
		case 2: draw_model(models.options.stopwatch, vec2(0, -0.2), vec3(0, 0, -400), system_cycle_time()); break;
		case MENU_RACE_TYPE_TWO_PLAYER:
			draw_model(models.misc.single_race, vec2(-0.25, -0.2), vec3(0, 0, -400), system_cycle_time());
			draw_model(models.misc.single_race, vec2( 0.25, -0.2), vec3(0, 0, -400), system_cycle_time() + 1.5);
			break;
	}
}

static void page_race_type_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "SELECT RACE TYPE", page_race_type_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;
	for (int i = 0; i < len(def.race_types); i++) {
		menu_page_add_button(page, i, def.race_types[i].name, button_race_type_select);
	}
	menu_page_add_button(page, MENU_RACE_TYPE_TWO_PLAYER, "TWO PLAYER RACE", button_race_type_select);
}



// -----------------------------------------------------------------------------
// Team

static void button_team_select(menu_t *menu, int data) {
	int player = MENU_DATA_PLAYER(data);
	if (player == 1) {
		g.team2 = MENU_DATA_VALUE(data);
	}
	else {
		g.team = MENU_DATA_VALUE(data);
	}
	page_pilot_init_for_player(menu, player);
}

static void page_team_draw(menu_t *menu, int data) {
	data = MENU_DATA_VALUE(data);
	int team_model_index = (data + 3) % 4; // models in the prm are shifted by -1
	draw_model(models.teams[team_model_index], vec2(0, -0.2), vec3(0, 0, -10000), system_cycle_time());
	draw_model(g.ships[def.teams[data].pilots[0]].model, vec2(0, -0.3), vec3(-700, -800, -1300), system_cycle_time()*1.1);
	draw_model(g.ships[def.teams[data].pilots[1]].model, vec2(0, -0.3), vec3( 700, -800, -1300), system_cycle_time()*1.2);
}

static void page_team_init(menu_t *menu) {
	page_team_init_for_player(menu, 0);
}

static void page_team_init_for_player(menu_t *menu, int player) {
	const char *title = g.num_players == 1
		? "SELECT YOUR TEAM"
		: (player == 1 ? "PLAYER 2 SELECT TEAM" : "PLAYER 1 SELECT TEAM");
	menu_page_t *page = menu_push(menu, (char *)title, page_team_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;
	for (int i = 0; i < len(def.teams); i++) {
		menu_page_add_button(page, MENU_PLAYER_DATA(player, i), def.teams[i].name, button_team_select);
	}
}



// -----------------------------------------------------------------------------
// Pilot

static void button_pilot_select(menu_t *menu, int data) {
	int player = MENU_DATA_PLAYER(data);
	data = MENU_DATA_VALUE(data);

	if (player == 1) {
		g.pilot2 = data;
		page_circuit_init(menu);
		return;
	}

	g.pilot = data;
	if (g.num_players > 1) {
		page_team_init_for_player(menu, 1);
	}
	else if (g.race_type != RACE_TYPE_CHAMPIONSHIP) {
		page_circuit_init(menu);
	}
	else {
		g.circuit = 0;
		game_reset_championship();
		game_set_scene(GAME_SCENE_RACE);
	}
}

static void page_pilot_draw(menu_t *menu, int data) {
	data = MENU_DATA_VALUE(data);
	draw_model(models.pilots[def.pilots[data].logo_model], vec2(0, -0.2), vec3(0, 0, -10000), system_cycle_time());
}

static void page_pilot_init(menu_t *menu) {
	page_pilot_init_for_player(menu, 0);
}

static void page_pilot_init_for_player(menu_t *menu, int player) {
	int team = player == 1 ? g.team2 : g.team;
	const char *title = g.num_players == 1
		? "CHOOSE YOUR PILOT"
		: (player == 1 ? "PLAYER 2 CHOOSE PILOT" : "PLAYER 1 CHOOSE PILOT");
	menu_page_t *page = menu_push(menu, (char *)title, page_pilot_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;
	for (int i = 0; i < len(def.teams[team].pilots); i++) {
		int pilot = def.teams[team].pilots[i];

		// Every pilot only exists once; player 2 can't have the one of player 1
		if (player == 1 && pilot == g.pilot) {
			continue;
		}
		menu_page_add_button(page, MENU_PLAYER_DATA(player, pilot), def.pilots[pilot].name, button_pilot_select);
	}
}


// -----------------------------------------------------------------------------
// Circuit

static void button_circuit_select(menu_t *menu, int data) {
	g.circuit = data;
	game_set_scene(GAME_SCENE_RACE);
}

static void page_circuit_additional_draw(menu_t *menu, int data) {}

static void page_circuit_additional_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "ADDITIONAL CIRCUITS", page_circuit_additional_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, 50);
	page->items_anchor = UI_POS_TOP | UI_POS_CENTER;

	for (int i = CIRCUIT_TALONS_REACH; i < len(def.circuits); i++) {
		if (g.installed_circuits[i] &&
			(!def.circuits[i].is_bonus_circuit || save.has_bonus_circuits)) {
			menu_page_add_button(page, i, def.circuits[i].name, button_circuit_select);
		}
	}
}

static void button_circuit_additional_select(menu_t *menu, int data) {
	page_circuit_additional_init(menu);
}

static void page_circuit_draw(menu_t *menu, int data) {
	vec2i_t pos = vec2i(0, -25);
	vec2i_t size = vec2i(128, 74);
	vec2i_t scaled_size = ui_scaled(size);
	vec2i_t scaled_pos = ui_scaled_pos(UI_POS_MIDDLE | UI_POS_CENTER, vec2i(pos.x - size.x/2, pos.y - size.y/2));
	render_push_2d(scaled_pos, scaled_size, rgba(128, 128, 128, 255), texture_from_list(track_images, data));
}

static void page_circuit_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "SELECT RACING CIRCUIT", page_circuit_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -100);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;
	for (int i = 0; i < CIRCUIT_TALONS_REACH; i++) {
		if (!def.circuits[i].is_bonus_circuit || save.has_bonus_circuits) {
			menu_page_add_button(page, i, def.circuits[i].name, button_circuit_select);
		}
	}
	if (g.additional_circuits) {
		menu_page_add_button(page, 0, "ADDITIONAL CIRCUITS", button_circuit_additional_select); 
	}
}

#define objects_unpack(DEST, SRC) \
	objects_unpack_imp((Object **)&DEST, sizeof(DEST)/sizeof(Object*), SRC)

static void objects_unpack_imp(Object **dest_array, int len, Object *src) {
	int i;
	for (i = 0; src && i < len; i++) {
		dest_array[i] = src;
		src = src->next;
	}
	error_if(i != len, "expected %d models got %d", len, i)
}


void main_menu_init(void) {
	g.is_attract_mode = false;

	ships_reset_exhaust_plumes();

	main_menu = mem_bump(sizeof(menu_t));

	background = image_get_texture("wipeout/textures/wipeout1.tim");
	track_images = image_get_compressed_textures("wipeout/textures/track.cmp");

	objects_unpack(models.race_classes, objects_load("wipeout/common/leeg.prm", image_get_compressed_textures("wipeout/common/leeg.cmp")));
	objects_unpack(models.teams, objects_load("wipeout/common/teams.prm", texture_list_empty()));
	objects_unpack(models.pilots, objects_load("wipeout/common/pilot.prm", image_get_compressed_textures("wipeout/common/pilot.cmp")));
	objects_unpack(models.options, objects_load("wipeout/common/alopt.prm", image_get_compressed_textures("wipeout/common/alopt.cmp")));
	objects_unpack(models.rescue, objects_load("wipeout/common/rescu.prm", image_get_compressed_textures("wipeout/common/rescu.cmp")));
	objects_unpack(models.controller, objects_load("wipeout/common/pad1.prm", image_get_compressed_textures("wipeout/common/pad1.cmp")));
	objects_unpack(models.misc, objects_load("wipeout/common/msdos.prm", image_get_compressed_textures("wipeout/common/msdos.cmp")));

	menu_reset(main_menu);
	page_main_init(main_menu);
}

void main_menu_update(void) {
	render_set_view_2d();
	render_push_2d(vec2i(0, 0), render_size(), rgba(128, 128, 128, 255), background);

	menu_update(main_menu);
}

