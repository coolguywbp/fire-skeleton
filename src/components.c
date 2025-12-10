#include <assert.h>
#include "components.h"
#include "main.h"


COMPONENT_IMPL(TransformComponent, ComponentStorageNormal)
void TransformComponent_new(TransformComponent *comp)
{
  comp->x = malloc(sizeof(float));
  comp->y = malloc(sizeof(float));
  *comp->x = (float) WINDOW_WIDTH / 2;
  *comp->y = (float) WINDOW_HEIGHT / 2;
}
void TransformComponent_free(TransformComponent *comp)
{
  if (comp->x) free(comp->x);
  if (comp->y) free(comp->y);
}

COMPONENT_IMPL(VelocityComponent, ComponentStorageNormal)
void VelocityComponent_new(VelocityComponent *comp)
{
  comp->vx = malloc(sizeof(float));
  comp->vy = malloc(sizeof(float));
  // *comp->vx = 0.1f;
  // *comp->vy = -0.2f;
  *comp->vx = random_float(-3, 3);
  *comp->vy = random_float(-3, 3);

}
void VelocityComponent_free(VelocityComponent *comp)
{
  if (comp->vx) free(comp->vx);
  if (comp->vy) free(comp->vy);

}


bool ecs_components_register(struct Game *G){
  bool res;
  
  res = REGISTER_COMPONENT(G->ecs, TransformComponent);
  assert(res);

  res = REGISTER_COMPONENT(G->ecs, VelocityComponent);
  assert(res);
  
  return true;
}


bool ecs_components_free(struct Game *G){
  return true;
}
