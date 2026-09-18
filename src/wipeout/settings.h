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

	bool split_vertical;
	bool swap_gamepads;
	bool external_view;
	int draw_distance; // 0..3: FULL, FAR, MEDIUM, NEAR

	bool is_dirty;
} settings_t;

extern settings_t settings;

void settings_load(void);
void settings_store_if_dirty(void);
void settings_apply(void); // pushes everything to the renderer and input
render_post_effect_t settings_post_flags(void);
void settings_set_dirty(void);

#endif
