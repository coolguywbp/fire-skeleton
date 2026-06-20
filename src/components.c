#include <assert.h>
#include "components.h"
#include "logger.h"
#include "game.h"

COMPONENT_IMPL(TransformComponent, ComponentStorageNormal)
void TransformComponent_new(TransformComponent *comp)
{
  comp->w = 50;
  comp->h = 50;
  // Random on-screen position, kept fully inside the window (accounting for size).
  comp->x = random_float(0, (float)WINDOW_WIDTH - comp->w);
  comp->y = random_float(0, (float)WINDOW_HEIGHT - comp->h);
}
void TransformComponent_free(TransformComponent *comp)
{
  (void)comp; // inline data, nothing to free
}

COMPONENT_IMPL(VelocityComponent, ComponentStorageNormal)
void VelocityComponent_new(VelocityComponent *comp)
{
  comp->vx = random_float(-5, 5);
  comp->vy = random_float(-5, 5);
}
void VelocityComponent_free(VelocityComponent *comp)
{
  (void)comp; // inline data, nothing to free
}

COMPONENT_IMPL(SpriteComponent, ComponentStorageNormal)
void SpriteComponent_new(SpriteComponent *comp)
{
  comp->gameImageId = 0; // default to the first loaded image
}
void SpriteComponent_free(SpriteComponent *comp)
{
  (void)comp; // inline data, nothing to free
}

void SpriteComponent_setGameImageId(SpriteComponent *comp, int gameImageId)
{
  comp->gameImageId = gameImageId;
}

COMPONENT_IMPL(CollisionComponent, ComponentStorageNormal)
void CollisionComponent_new(CollisionComponent *comp)
{
  comp->layer = 0;
}
void CollisionComponent_free(CollisionComponent *comp)
{
  (void)comp; // inline data, nothing to free
}



bool ecs_components_register(struct Game *G){
  bool res;
  int idx = 0;

  res = REGISTER_COMPONENT(G->ecs, TransformComponent);
  assert(res);
  res = REGISTER_COMPONENT(G->ecs, VelocityComponent);
  assert(res);
  res = REGISTER_COMPONENT(G->ecs, SpriteComponent);
  assert(res);
  res = REGISTER_COMPONENT(G->ecs, CollisionComponent);
  assert(res);

  return true;
}


bool ecs_components_free(struct Game *G){
  return true;
}
