#ifndef ARCHETYPES_H
#define ARCHETYPES_H
#include "game.h"

typedef enum{
  TEST_BOUNCING_SPRITE_ARCHETYPE = 0,
  PLAYER_ARCHETYPE,
  MAX_ARCHETYPES
} Archetypes;

bool load_archetypes(struct Game *G);
#endif
