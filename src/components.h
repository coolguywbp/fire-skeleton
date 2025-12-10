#ifndef COMPONENTS_H
#define COMPONENTS_H

#include "component.h"
#include "game.h"

COMPONENT(TransformComponent)
struct TransformComponent {
  float *x, *y;
};

COMPONENT(VelocityComponent)
struct VelocityComponent {
	float *vx, *vy;
};

// COMPONENT(RenderRectangleComponent)
// struct RenderRectangleComponent {
// 	int* w,* h;
// };
//
bool ecs_components_register(struct Game *G);
bool ecs_components_free(struct Game *G);

#endif /* end of include guard: ECS_TESTCOMPONENT_H */
