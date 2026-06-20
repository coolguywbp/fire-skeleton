#pragma once
#ifndef SCRIPT_H
#define SCRIPT_H

#include <stdbool.h>

#include "ecs_core.h"   // Entity

struct Game;
struct lua_State;

// The Game behind a lua_State (for other modules' Lua C functions, e.g. ui_lua).
struct Game *script_game(struct lua_State *L);

// The Lua scripting layer.
//
// Owns a single Lua state created on the main thread. Lua is NOT thread-safe,
// so every call into Lua (callbacks, the C API exposed to scripts) must happen
// on the main thread only — never from an ECS worker-pool system.
//
// Design split: hot, uniform per-entity work (motion, rendering of thousands of
// sprites) stays in C systems; scripts drive game-level logic and orchestration
// (spawning, waves, events) where call frequency is low. Scripts must never be
// invoked once-per-entity-per-frame on the hot path.
typedef struct Script Script;

// Create the Lua state, register the C API and engine prelude, but load no
// gameplay script (idle state). Modes load their script later via script_load.
// Returns false on failure (the caller may continue without scripting).
bool script_init(struct Game *G);

// Tear down the current script/scene and load `path` (running its on_start).
// On failure the current script keeps running unchanged. Returns success.
bool script_load(struct Game *G, const char *path);

// The path of the currently loaded script, or NULL if idle.
const char *script_current_path(struct Game *G);

// Tear down the current scene/script back to the idle state and clear the HUD.
void script_unload(struct Game *G);

// Run game-level per-frame logic: calls the Lua on_update(dt) callback if
// present. Invoked ONCE per frame (in the level), never per entity.
void script_update(struct Game *G, float dt);

// Dispatch a detected collision between two entities to the Lua on_collision(a, b)
// callback, if the script defines one. Main thread only.
void script_on_collision(struct Game *G, Entity a, Entity b);

// Dispatch a key press (lowercased SDL key name, e.g. "space", "left") to the
// Lua on_key(key) callback, if defined. Main thread only.
void script_on_key(struct Game *G, const char *key);

// Call the script's on_ui() callback so it can draw its UI for this frame.
// Must be invoked inside the Clay layout pass.
void script_on_ui(struct Game *G);

// Rebuild the script world from its file. The fresh state is loaded first and
// swapped in only if it loads cleanly; on failure the running game is unchanged.
bool script_reload(struct Game *G);

// Poll the script file for changes (throttled) and hot-reload when it changes.
// Call once per frame.
void script_check_reload(struct Game *G);

// Close the Lua state and free the Script. Safe to call with G->script == NULL.
void script_free(struct Game *G);

#endif
