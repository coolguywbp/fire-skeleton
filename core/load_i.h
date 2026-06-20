#include "game.h"

// Number of textures load_images() loads (and game_free destroys). Keep in sync
// with the image_names[] array in load_i.c.
#define IMAGE_COUNT 5

SDL_Texture **load_images(SDL_Renderer *renderer);
