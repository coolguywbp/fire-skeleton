#include "init_ecs.h"
#include "archetypes.h"
#include "game.h"
#include "logger.h"

bool init_ecs (struct Game *G)
{
	G->ecs = ECS_New();

  if(!ecs_components_register(G)){

    LOG_ERROR("ECS components register failed");
    return false;
  }else{
    LOG_DEBUG("ECS components OK");
  }

  if (!load_archetypes(G)){
    LOG_ERROR("ECS archetypes register failed");
    return false;
  }else{
    LOG_DEBUG("ECS archetypes OK");
  }

	if(!ecs_systems_register(G)){
    LOG_ERROR("ECS systems register failed");
    return false;
  }else{
    LOG_DEBUG("ECS systems OK");
  };

	bool res = ECS_SetThreads(G->ecs, 1);
	assert(res);

	return true;
}
