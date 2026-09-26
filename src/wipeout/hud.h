#ifndef HUD_H
#define HUD_H

#include "ship.h"

void hud_load(void);
void hud_draw(ship_t *ship);
void hud_draw_player_marker(ship_t *ship, const char *label);

#endif
