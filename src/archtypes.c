#include "archetypes.h"
#include "ecs_entity.h"
#include "game.h"

bool load_archetypes(struct Game *G){
  /////////////////////////////////////////////////////
  // TestBouncingSprite
  const char *TestBouncingSprite_components[] = {
	"TransformComponent",
  "VelocityComponent",
  "SpriteComponent",
  NULL};
  
  /////////////////////////////////////////////////////
  // Player
  const char *Player_components[] = {
	"TransformComponent",
  "SpriteComponent",
  NULL};
  
  G->archetypes = malloc(sizeof(EntityArchetype*) * MAX_ARCHETYPES);
  G->archetypes[TEST_BOUNCING_SPRITE_ARCHETYPE] = ECS_EntityRegisterArchetype(G->ecs, "TestBouncingSprite", TestBouncingSprite_components);
  G->archetypes[PLAYER_ARCHETYPE] = ECS_EntityRegisterArchetype(G->ecs, "Player", Player_components);

  return true;
}
