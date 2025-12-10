#pragma once
#ifndef SYSTEMS_H
#define SYSTEMS_H

#include "system.h"

SYSTEM(VelocitySystem);
struct VelocitySystem {
    // Nothing to see here, move along.
};

bool ecs_systems_register(struct Game *G);
bool ecs_systems_free(struct Game *G);
#endif
