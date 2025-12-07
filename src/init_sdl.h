#ifndef INIT_SDL_H
#define INIT_SDL_H

#include "game.h"

bool game_init_sdl(struct Game *G);
TTF_Font **load_fonts(struct Game *G);

#endif
