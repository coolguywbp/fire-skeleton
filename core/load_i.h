#include "game.h"

// Load every PNG under assets/images/ as a texture. Returns the texture array
// (caller owns it); *out_names receives a parallel array of image names (the
// file name without the .png extension), and *out_count the number loaded.
// Images are addressed from scripts by name (see image_id_by_name); the numeric
// id is just the index into these arrays.
SDL_Texture **load_images(SDL_Renderer *renderer, char ***out_names, int *out_count);

// Resolve an image name to its id (index), or -1 if there's no such image.
int image_id_by_name(struct Game *G, const char *name);
