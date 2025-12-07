#ifndef MAIN_H
#define MAIN_H
#define _POSIX_C_SOURCE 200112L // needed to get setenv() working

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "clay.h"

#define SDL_FLAGS SDL_INIT_VIDEO
#define MIX_FLAGS MIX_INIT_OGG

// Comment for production build
#define DEV_MODE

#define SHOW_FPS

#define WINDOW_TITLE "FIRE SKELETON INVADERS"
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 960

#define WHITE_COLOR (SDL_Color){255, 255, 255, 255}
#define BLACK_COLOR (SDL_Color){0, 0, 0, 255}
#define BLUE_COLOR (SDL_Color){137, 180, 250, 255}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


#endif
