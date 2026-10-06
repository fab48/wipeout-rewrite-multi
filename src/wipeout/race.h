#ifndef RACE_H
#define RACE_H

#include "ship.h"
#include "menu.h"

void race_init(void);
void race_update(void);
void race_start(void);
void race_restart(void);
void race_pause(void);
void race_unpause(void);
void race_end(void);
void race_player_finished(ship_t *ship);

// A short lived point light, for explosions. particle_type picks the color.
void race_add_flash_light(vec3_t pos, int particle_type);
void race_next(void);
void race_release_control(void);
void race_release_ship(ship_t *ship);
void race_show_menu(menu_t *menu);
bool race_menu_is_open(void);

#endif
