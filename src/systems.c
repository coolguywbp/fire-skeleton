#include "components.h"
#include "game.h"
#include "init_ecs.h"
#include "main.h"
#include <assert.h>
#include <stdio.h>

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
  

  // System Logic
  *transform->x += *velocity->vx;
  *transform->y += *velocity->vy;

  if (*transform->x <= 0 || *transform->x >= WINDOW_WIDTH) {*velocity->vx = -*velocity->vx;}
  if (*transform->y <= 0 || *transform->y >= WINDOW_HEIGHT) {*velocity->vy = -*velocity->vy;}

}
bool VelocitySystem_event(Event *event, VelocitySystem *system)
{
	return false;
}

const SystemUpdateInfo VelocitySystem_update_info = {
	true, false, false, NULL
};

bool ecs_systems_register(struct Game *G){
  const char *BouncingRect_components[] = {
	"TransformComponent", "VelocityComponent", NULL
  };

  VelocitySystem_reg.archetype = ECS_EntityRegisterArchetype(G->ecs, "BouncingRectArchetype", BouncingRect_components);
  
  bool res;
  VelocitySystem *velocity_sys = malloc(sizeof(VelocitySystem));
	res = REGISTER_SYSTEM(G->ecs, VelocitySystem, velocity_sys);
	assert(res);
  
  return true;
}

bool ecs_systems_free(struct Game *G){
  return true;
}


