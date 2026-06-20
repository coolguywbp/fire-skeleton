#include "load_m.h"
#include "load_i.h"

bool game_load_media(struct Game *G) {
  G->images = load_images(G->renderer);
  return true;
}


