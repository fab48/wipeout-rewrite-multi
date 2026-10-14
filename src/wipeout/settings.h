#ifndef SETTINGS_H
#define SETTINGS_H

#include "../types.h"
#include "../render.h"

// Render and gameplay settings added by this fork. They live in a small text
// file (settings.txt) next to save.dat, so that the binary save_t (and with
// it the highscores) never has to change. Older saves that stored these as
// bits in save.post_effect are migrated on first load.
typedef struct {
	bool crt;
	bool bloom;
	int bloom_threshold; // 0..3: DEFAULT, LOW, LOWER, HIGH
	float bloom_intensity; // multiplier, 1.0 = default
	bool motion_blur;
	float motion_blur_strength; // multiplier
	bool tonemap;
	bool lighting; // PBR
	float lighting_brightness; // multiplier
	int point_lights; // 0..6
	bool texture_smooth; // linear magnification instead of the pixelated PSX look
	bool texture_upscale; // xBR 2x on all textures at load time

	bool split_vertical;
	bool swap_gamepads;
	bool external_view;
	int draw_distance; // 0..3: FULL, FAR, MEDIUM, NEAR

	int net_pilot; // preferred pilot in LAN games
	bool net_drop_pause; // LAN host: a player drops out: pause the race (or let the AI drive)

	int graphics_preset; // graphics_preset_t

	bool is_dirty;
} settings_t;

extern settings_t settings;

// Graphics presets. Each one sets the render effects, the resolution and the
// draw distance. Changing one of those by hand shows CUSTOM, unless the new
// combination happens to match a preset.
typedef enum {
	GRAPHICS_PRESET_ORIGINAL, // CRT, 480p, medium draw distance, no modern effects
	GRAPHICS_PRESET_LOW,      // 720p, full draw distance, no post processing (old PCs)
	GRAPHICS_PRESET_HQ,       // every effect on, native resolution
	GRAPHICS_PRESET_CUSTOM,
	NUM_GRAPHICS_PRESETS
} graphics_preset_t;

void settings_apply_graphics_preset(int preset);
void settings_graphics_changed(void); // a single graphics option was changed by hand

void settings_load(void);
void settings_store_if_dirty(void);
void settings_apply(void); // pushes everything to the renderer and input
render_post_effect_t settings_post_flags(void);
void settings_set_dirty(void);

#endif
