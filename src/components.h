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

// Opt-in marker: only entities carrying this component take part in collision
// detection (so the benchmark's bouncing sprites, which lack it, are skipped).
// The AABB comes from the entity's TransformComponent. `layer` is reserved for
// future layer/mask filtering.
COMPONENT(CollisionComponent)
struct CollisionComponent {
  uint32_t layer;
};

bool ecs_components_register(struct Game *G);
bool ecs_components_free(struct Game *G);

#endif
