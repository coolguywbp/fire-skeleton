#pragma once
#ifndef SYSTEMS_H
#define SYSTEMS_H

#include "ecs_system.h"
#include "game.h"

SYSTEM(SpriteRenderSystem)
struct SpriteRenderSystem {
  SDL_Renderer *renderer;
  SDL_Texture **images;
};

SYSTEM(VelocitySystem)
struct VelocitySystem {
  const Mouse *mouse; // sprites are repelled from a circle around the cursor
};

bool ecs_systems_register(struct Game *G);
bool ecs_systems_free(struct Game *G);
#endif
