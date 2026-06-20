#pragma once
#ifndef COLLISION_H
#define COLLISION_H

struct Game;

// Broad-phase (uniform spatial hash) + AABB narrow-phase over every entity that
// has a CollisionComponent. Each overlapping pair is dispatched once to the Lua
// on_collision(a, b) callback.
//
// Call once per frame in the level, AFTER movement, on the MAIN THREAD (it
// calls into Lua, which is not thread-safe).
void collision_update(struct Game *G);

#endif
