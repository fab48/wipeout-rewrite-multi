#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../mem.h"
#include "../utils.h"
#include "../platform.h"
#include "../input.h"
#include "../render.h"

#include "settings.h"
#include "game.h"

#define SETTINGS_FILE "settings.txt"

settings_t settings = {
	.crt = false,
	.bloom = true,
	.bloom_threshold = 2, // LOWER
	.bloom_intensity = 1.0,
	.motion_blur = true,
	.motion_blur_strength = 1.0,
	.tonemap = true,
	.lighting = true,
	.lighting_brightness = 1.0,
	.point_lights = 4,

	.split_vertical = false,
	.swap_gamepads = false,
	.external_view = false,
	.draw_distance = 0,

	.is_dirty = false,
};

typedef enum { SETTING_BOOL, SETTING_INT, SETTING_FLOAT } setting_type_t;

typedef struct {
	const char *key;
	setting_type_t type;
	void *value;
	float min, max;
} setting_def_t;

static const setting_def_t setting_defs[] = {
	{"crt",                  SETTING_BOOL,  &settings.crt, 0, 1},
	{"bloom",                SETTING_BOOL,  &settings.bloom, 0, 1},
	{"bloom_threshold",      SETTING_INT,   &settings.bloom_threshold, 0, 3},
	{"bloom_intensity",      SETTING_FLOAT, &settings.bloom_intensity, 0.1, 4.0},
	{"motion_blur",          SETTING_BOOL,  &settings.motion_blur, 0, 1},
	{"motion_blur_strength", SETTING_FLOAT, &settings.motion_blur_strength, 0.1, 3.0},
	{"tonemap",              SETTING_BOOL,  &settings.tonemap, 0, 1},
	{"lighting",             SETTING_BOOL,  &settings.lighting, 0, 1},
	{"lighting_brightness",  SETTING_FLOAT, &settings.lighting_brightness, 0.25, 3.0},
	{"point_lights",         SETTING_INT,   &settings.point_lights, 0, 6},
	{"split_vertical",       SETTING_BOOL,  &settings.split_vertical, 0, 1},
	{"swap_gamepads",        SETTING_BOOL,  &settings.swap_gamepads, 0, 1},
	{"external_view",        SETTING_BOOL,  &settings.external_view, 0, 1},
	{"draw_distance",        SETTING_INT,   &settings.draw_distance, 0, 3},
};

static void setting_set(const setting_def_t *def, float value) {
	value = clamp(value, def->min, def->max);
	switch (def->type) {
	case SETTING_BOOL: *(bool *)def->value = value > 0.5; break;
	case SETTING_INT: *(int *)def->value = (int)(value + 0.5); break;
	case SETTING_FLOAT: *(float *)def->value = value; break;
	}
}

static float setting_get(const setting_def_t *def) {
	switch (def->type) {
	case SETTING_BOOL: return *(bool *)def->value ? 1 : 0;
	case SETTING_INT: return *(int *)def->value;
	case SETTING_FLOAT: return *(float *)def->value;
	}
	return 0;
}

// Older versions kept these as bits in save.post_effect
static void settings_migrate_from_post_effect(int post) {
	if (post == 0) {
		return; // never configured: keep the defaults
	}
	settings.crt = post & RENDER_POST_CRT;
	settings.bloom = post & RENDER_POST_BLOOM;
	settings.motion_blur = post & RENDER_POST_MOTION_BLUR;
	settings.lighting = post & RENDER_POST_LIGHTING;
	settings.point_lights = (post & RENDER_POST_POINT_LIGHTS_MASK) >> RENDER_POST_POINT_LIGHTS_SHIFT;
	settings.bloom_threshold = (post & RENDER_POST_BLOOM_THRESHOLD_MASK) >> RENDER_POST_BLOOM_THRESHOLD_SHIFT;
	settings.tonemap = !(post & RENDER_POST_NO_TONEMAP);
	settings.split_vertical = post & RENDER_POST_SPLIT_VERTICAL;
	settings.swap_gamepads = post & RENDER_POST_SWAP_GAMEPADS;
	settings.external_view = post & RENDER_POST_EXTERNAL_VIEW;
	settings.draw_distance = (post & RENDER_POST_DRAW_DISTANCE_MASK) >> RENDER_POST_DRAW_DISTANCE_SHIFT;
	settings.is_dirty = true; // write the text file right away
}

void settings_load(void) {
	uint32_t size = 0;
	uint8_t *bytes = platform_load_userdata(SETTINGS_FILE, &size);
	if (!bytes) {
		settings_migrate_from_post_effect(save.post_effect);
		return;
	}

	// One "key = value" per line
	char *text = mem_temp_alloc(size + 1);
	memcpy(text, bytes, size);
	text[size] = '\0';
	mem_temp_free(bytes);

	char *line = text;
	while (line && *line) {
		char *next = strchr(line, '\n');
		if (next) {
			*next++ = '\0';
		}
		char key[64];
		float value;
		if (sscanf(line, " %63[a-z_] = %f", key, &value) == 2) {
			for (int i = 0; i < len(setting_defs); i++) {
				if (strcmp(setting_defs[i].key, key) == 0) {
					setting_set(&setting_defs[i], value);
				}
			}
		}
		line = next;
	}
	mem_temp_free(text);
	settings.is_dirty = false;
}

void settings_store_if_dirty(void) {
	if (!settings.is_dirty) {
		return;
	}
	settings.is_dirty = false;

	char text[2048];
	int p = 0;
	p += snprintf(text + p, sizeof(text) - p, "# wipEout rewrite (multi/vfx branch) settings\n");
	for (int i = 0; i < len(setting_defs) && p < (int)sizeof(text) - 64; i++) {
		const setting_def_t *def = &setting_defs[i];
		if (def->type == SETTING_FLOAT) {
			p += snprintf(text + p, sizeof(text) - p, "%s = %.3f\n", def->key, setting_get(def));
		}
		else {
			p += snprintf(text + p, sizeof(text) - p, "%s = %d\n", def->key, (int)setting_get(def));
		}
	}
	platform_store_userdata(SETTINGS_FILE, text, p);
	printf("wrote %s\n", SETTINGS_FILE);
}

void settings_set_dirty(void) {
	settings.is_dirty = true;
	settings_apply();
}

render_post_effect_t settings_post_flags(void) {
	int post = 0;
	if (settings.crt) post |= RENDER_POST_CRT;
	if (settings.bloom) post |= RENDER_POST_BLOOM;
	if (settings.motion_blur) post |= RENDER_POST_MOTION_BLUR;
	if (settings.lighting) post |= RENDER_POST_LIGHTING;
	if (!settings.tonemap) post |= RENDER_POST_NO_TONEMAP;
	if (settings.split_vertical) post |= RENDER_POST_SPLIT_VERTICAL;
	if (settings.swap_gamepads) post |= RENDER_POST_SWAP_GAMEPADS;
	if (settings.external_view) post |= RENDER_POST_EXTERNAL_VIEW;
	post |= clamp(settings.point_lights, 0, 6) << RENDER_POST_POINT_LIGHTS_SHIFT;
	post |= clamp(settings.bloom_threshold, 0, 3) << RENDER_POST_BLOOM_THRESHOLD_SHIFT;
	post |= clamp(settings.draw_distance, 0, 3) << RENDER_POST_DRAW_DISTANCE_SHIFT;
	return post;
}

void settings_apply(void) {
	render_set_post_effect(settings_post_flags());
	render_set_post_params(settings.bloom_intensity, settings.motion_blur_strength, settings.lighting_brightness);
	input_set_gamepad_swap(settings.swap_gamepads);
}
