#pragma once
#ifndef SYSTEMS_H
#define SYSTEMS_H

#include "ecs_system.h"

SYSTEM(SpriteRenderSystem)
struct SpriteRenderSystem {};

SYSTEM(VelocitySystem)
struct VelocitySystem {};

bool ecs_systems_register(struct Game *G);
bool ecs_systems_free(struct Game *G);
#endif
