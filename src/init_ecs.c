#include "init_ecs.h"
#include "components.h"
#include "systems.h"
#include "archetypes.h"

#include "game.h"

bool init_ecs (struct Game *G)
{
	G->ecs = ECS_New();

  ecs_components_register(G);
  

  // ecs_archetypes_register(G);

	ecs_systems_register(G);

	bool res = ECS_SetThreads(G->ecs, 1);
	assert(res);

	return true;
}
