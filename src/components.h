#pragma once
#ifndef COMPONENTS_H
#define COMPONENTS_H

#include "ecs_component.h"
#include "game.h"


// Component data is stored inline (not as pointers to heap) so that it lives
// contiguously inside the component arrays — far better cache behaviour and no
// per-field allocations.
COMPONENT(TransformComponent)
struct TransformComponent {
  float x, y, w, h;
};

COMPONENT(VelocityComponent)
struct VelocityComponent {
	float vx, vy;
};

COMPONENT(SpriteComponent)
struct SpriteComponent{
  int gameImageId;
  // int currentSpriteIndex;
};

bool ecs_components_register(struct Game *G);
bool ecs_components_free(struct Game *G);

#endif
