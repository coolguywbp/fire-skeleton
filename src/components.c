#include <assert.h>
#include "components.h"
#include "logger.h"
#include "game.h"


COMPONENT_IMPL(TransformComponent, ComponentStorageNormal)
void TransformComponent_new(TransformComponent *comp)
{
  comp->x = malloc(sizeof(float));
  comp->y = malloc(sizeof(float));
  comp->w = malloc(sizeof(float));
  comp->h = malloc(sizeof(float));

  *comp->x = (float) WINDOW_WIDTH / 2;
  *comp->y = (float) WINDOW_HEIGHT / 2;
  *comp->w = 50;
  *comp->h = 50;
}
void TransformComponent_free(TransformComponent *comp)
{
  if (comp->x) free(comp->x);
  if (comp->y) free(comp->y);
  if (comp->w) free(comp->w);
  if (comp->h) free(comp->h);
}

COMPONENT_IMPL(VelocityComponent, ComponentStorageNormal)
void VelocityComponent_new(VelocityComponent *comp)
{
  comp->vx = malloc(sizeof(float));
  comp->vy = malloc(sizeof(float));
  *comp->vx = random_float(-5, 5);
  *comp->vy = random_float(-5, 5);

}
void VelocityComponent_free(VelocityComponent *comp)
{
  if (comp->vx) free(comp->vx);
  if (comp->vy) free(comp->vy);
}

COMPONENT_IMPL(SpriteComponent, ComponentStorageNormal)
void SpriteComponent_new(SpriteComponent *comp)
{
  comp->gameImageId = malloc(sizeof(int));
}
void SpriteComponent_free(SpriteComponent *comp)
{
  if (comp->gameImageId) free(comp->gameImageId);
}

void SpriteComponent_setGameImageId(SpriteComponent *comp, int gameImageId)
{
  *comp->gameImageId = gameImageId;
}



bool ecs_components_register(struct Game *G){
  bool res;

  res = REGISTER_COMPONENT(G->ecs, TransformComponent);
  assert(res);
  LOG_DEBUG("ECS TransformComponent OK");
  res = REGISTER_COMPONENT(G->ecs, VelocityComponent);
  assert(res);
  LOG_DEBUG("ECS VelocityComponent OK");
  res = REGISTER_COMPONENT(G->ecs, SpriteComponent);
  assert(res);
  LOG_DEBUG("ECS SpriteComponent OK");
  
  return true;
}


bool ecs_components_free(struct Game *G){
  return true;
}
