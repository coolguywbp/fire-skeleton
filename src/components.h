#pragma once
#ifndef COMPONENTS_H
#define COMPONENTS_H

#include "ecs_component.h"
#include "game.h"

COMPONENT(TransformComponent)
struct TransformComponent {
  float *x, *y, *w, *h;
};

COMPONENT(VelocityComponent)
struct VelocityComponent {
	float *vx, *vy;
};

COMPONENT(SpriteComponent)
struct SpriteComponent{
  int *gameImageId;
  // int *currentSpriteIndex;
};

bool ecs_components_register(struct Game *G);
bool ecs_components_free(struct Game *G);

#endif
