#include "components.h"
#include "systems.h"
#include <assert.h>

SYSTEM_IMPL(SpriteRenderSystem)
void SpriteRenderSystem_update(Entity e, Component **c, SpriteRenderSystem *system)
{
  assert(c[0]);
}

bool SpriteRenderSystem_event(Event *event, SpriteRenderSystem *system)
{
	return false;
}

const char *SpriteRenderSystem_afterSystems[] = {"VelocitySystem"};
const SystemUpdateInfo SpriteRenderSystem_update_info = {
	true, false, false, SpriteRenderSystem_afterSystems};


SYSTEM_IMPL(VelocitySystem)
void VelocitySystem_update(Entity e, Component **c, VelocitySystem *system)
{

  printf("Velocity system update\n");
	// TransformComponent
	assert(c[0]);
  TransformComponent *transform = c[0];
  printf("Entity %d: (transform: %fx, %fy)\n", (int)e, *transform->x, *transform->y);
  
  
  //VelocityComponent
  assert(c[1]);
  VelocityComponent *velocity = c[1];
  
  const float EPSILON = 0.001f;
  // System Logic
  *transform->x += *velocity->vx;
  *transform->y += *velocity->vy;

  // X bounds with epsilon
  if (*transform->x < -EPSILON) {
      *transform->x = -*transform->x - 2 * EPSILON;
      *velocity->vx = -*velocity->vx;
  } 
  else if (*transform->x > WINDOW_WIDTH + EPSILON) {
      *transform->x = 2 * WINDOW_WIDTH - *transform->x + 2 * EPSILON;
      *velocity->vx = -*velocity->vx;
  }

  // Y bounds
  if (*transform->y < -EPSILON) {
      *transform->y = -*transform->y - 2 * EPSILON;
      *velocity->vy = -*velocity->vy;
  } 
  else if (*transform->y > WINDOW_HEIGHT + EPSILON) {
      *transform->y = 2 * WINDOW_HEIGHT - *transform->y + 2 * EPSILON;
      *velocity->vy = -*velocity->vy;
  }
}

bool VelocitySystem_event(Event *event, VelocitySystem *system)
{
	return false;
}

const char *VelocitySystem_afterSystems = NULL;
const SystemUpdateInfo VelocitySystem_update_info = {
	true, false, false, NULL};

bool ecs_systems_register(struct Game *G){
  //////////////////////////////////
  //
  //    SPRITE RENDER SYSTEM
  //
  const char *SpriteRenderSystem_components[] = {
	 "TransformComponent", "SpriteComponent", NULL};
	SpriteRenderSystem_reg.archetype = ECS_EntityRegisterArchetype(G->ecs, "SpriteRenderSystemArchetype", SpriteRenderSystem_components);
	SpriteRenderSystem *spriteRender_sys = malloc(sizeof(SpriteRenderSystem));
	assert(REGISTER_SYSTEM(G->ecs, SpriteRenderSystem, spriteRender_sys));
  LOG_DEBUG("ECS SpriteRenderSystem OK");
  //////////////////////////////////
  //
  //    VELOCITY SYSTEM
  //
  const char *VelocitySystem_components[] = {
	"TransformComponent", "VelocityComponent", NULL};
  VelocitySystem_reg.archetype = ECS_EntityRegisterArchetype(G->ecs, "VelocitySystemArchetype", VelocitySystem_components);
  VelocitySystem *velocity_sys = malloc(sizeof(VelocitySystem));
	assert(REGISTER_SYSTEM(G->ecs, VelocitySystem, velocity_sys));
  LOG_DEBUG("ECS VelocitySystem OK");
  //////////////////////////////////


  return true;
}

bool ecs_systems_free(struct Game *G){
  return true;
}


