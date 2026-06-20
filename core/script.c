#include "script.h"

#include "game.h"
#include "logger.h"

#include "components.h"   // TransformComponent / VelocityComponent / SpriteComponent + COMPONENT_ID
#include "ecs_entity.h"   // ECS_EntityNew / Delete / GetComponent / Exists / RegisterArchetype
#include "ui_lua.h"       // ui.* immediate-mode toolkit
#include "g3d.h"          // g3d.* software 3D primitives
#include "load_i.h"       // image_id_by_name (name -> image id)

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Prefab model
//
// A prefab is a named template: a set of components plus default field values.
// Defining one (from Lua) registers a matching ECS archetype, owned by the
// Script and freed in script_free. spawn() then creates an entity from that
// archetype and overrides the fields the prefab specified.
//
// Fields use explicit `has_*` flags: a field the prefab omits is left at the
// component constructor's default (e.g. random position/velocity), so a prefab
// can pin only what it cares about.
// ---------------------------------------------------------------------------

typedef struct {
  bool has_x, has_y, has_w, has_h;
  float x, y, w, h;
} PrefabTransform;

typedef struct {
  bool has_vx, has_vy;
  float vx, vy;
} PrefabVelocity;

typedef struct {
  bool has_image;
  int image;
} PrefabSprite;

typedef struct {
  char *name;
  EntityArchetype *archetype;

  bool has_transform, has_velocity, has_sprite, has_collision;
  PrefabTransform tr;
  PrefabVelocity  vel;
  PrefabSprite    spr;

  // Entities spawned from this prefab (for count()/destroy() bookkeeping).
  Entity *ents;
  size_t  ent_count, ent_cap;
} Prefab;

struct Script {
  lua_State *L;
  struct Game *G;        // so C API functions can reach the ECS

  Prefab *prefabs;
  size_t  prefab_count, prefab_cap;

  // Hot-reload watcher.
  char    *path;         // script file, for reloading
  SDL_Time last_mtime;   // last seen modification time
  double   watch_accum;  // throttles the mtime check
};

// Stash/fetch the owning Script in the Lua state's extra space (one pointer).
static Script *get_script(lua_State *L) {
  return *(Script **)lua_getextraspace(L);
}

// Public accessor for other modules' Lua C functions (e.g. ui_lua.c) to reach
// the Game behind a lua_State.
struct Game *script_game(lua_State *L) {
  Script *s = *(Script **)lua_getextraspace(L);
  return s ? s->G : NULL;
}

// strdup is POSIX, not in -std=c11; provide a local equivalent.
static char *str_dup(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}

// ---------------------------------------------------------------------------
// Prefab registry helpers
// ---------------------------------------------------------------------------

static Prefab *find_prefab(Script *s, const char *name) {
  for (size_t i = 0; i < s->prefab_count; i++)
    if (strcmp(s->prefabs[i].name, name) == 0) return &s->prefabs[i];
  return NULL;
}

static void prefab_track(Prefab *p, Entity e) {
  if (p->ent_count == p->ent_cap) {
    size_t nc = p->ent_cap ? p->ent_cap * 2 : 16;
    Entity *q = realloc(p->ents, nc * sizeof(Entity));
    if (!q) return; // drop tracking on OOM; the entity still lives in the ECS
    p->ents = q;
    p->ent_cap = nc;
  }
  p->ents[p->ent_count++] = e;
}

// Remove an entity from whichever prefab's tracking list holds it (swap-remove).
static void prefab_untrack(Script *s, Entity e) {
  for (size_t pi = 0; pi < s->prefab_count; pi++) {
    Prefab *p = &s->prefabs[pi];
    for (size_t i = 0; i < p->ent_count; i++) {
      if (p->ents[i] == e) {
        p->ents[i] = p->ents[--p->ent_count];
        return;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Applying prefab defaults to a freshly created entity
// ---------------------------------------------------------------------------

static void apply_prefab(Script *s, Prefab *p, Entity e,
                         bool override_pos, float px, float py) {
  ECS *ecs = s->G->ecs;

  // Each component is only fetched if the prefab actually sets a field on it;
  // an empty component table (e.g. Velocity = {}) leaves the C constructor's
  // values untouched and pays no per-spawn lookup. This keeps bulk spawning
  // (the benchmark) as cheap as a raw ECS_EntityNew.
  if (p->has_transform &&
      (override_pos || p->tr.has_x || p->tr.has_y || p->tr.has_w || p->tr.has_h)) {
    TransformComponent *t = ECS_EntityGetComponent(ecs, e, COMPONENT_ID(TransformComponent));
    if (t) {
      if (p->tr.has_w) t->w = p->tr.w;
      if (p->tr.has_h) t->h = p->tr.h;
      if (p->tr.has_x) t->x = p->tr.x;
      if (p->tr.has_y) t->y = p->tr.y;
      if (override_pos) { t->x = px; t->y = py; }
    }
  }
  if (p->has_velocity && (p->vel.has_vx || p->vel.has_vy)) {
    VelocityComponent *v = ECS_EntityGetComponent(ecs, e, COMPONENT_ID(VelocityComponent));
    if (v) {
      if (p->vel.has_vx) v->vx = p->vel.vx;
      if (p->vel.has_vy) v->vy = p->vel.vy;
    }
  }
  if (p->has_sprite && p->spr.has_image) {
    SpriteComponent *sp = ECS_EntityGetComponent(ecs, e, COMPONENT_ID(SpriteComponent));
    if (sp) sp->gameImageId = p->spr.image;
  }
}

// ---------------------------------------------------------------------------
// C API exposed to Lua
// ---------------------------------------------------------------------------

// log(msg) -- write a message to the engine logger.
static int l_log(lua_State *L) {
  LOG_INFO("[lua] %s", luaL_checkstring(L, 1));
  return 0;
}

// Read a numeric field `key` from the table at stack index `t`. Returns true
// and writes *out if present and numeric.
static bool get_num_field(lua_State *L, int t, const char *key, float *out) {
  lua_getfield(L, t, key);
  bool ok = lua_isnumber(L, -1);
  if (ok) *out = (float)lua_tonumber(L, -1);
  lua_pop(L, 1);
  return ok;
}

// Second stage of `prefab "Name" { ... }`: receives the definition table; the
// name is bound as upvalue 1.
static int l_prefab_define(lua_State *L) {
  Script *s = get_script(L);
  const char *name = lua_tostring(L, lua_upvalueindex(1));
  luaL_checktype(L, 1, LUA_TTABLE);
  const int def = 1;

  Prefab pf;
  memset(&pf, 0, sizeof pf);
  pf.name = str_dup(name);

  const char *comps[5];
  int nc = 0;

  lua_getfield(L, def, "Transform");
  if (lua_istable(L, -1)) {
    int t = lua_gettop(L);
    pf.has_transform = true;
    pf.tr.has_x = get_num_field(L, t, "x", &pf.tr.x);
    pf.tr.has_y = get_num_field(L, t, "y", &pf.tr.y);
    pf.tr.has_w = get_num_field(L, t, "w", &pf.tr.w);
    pf.tr.has_h = get_num_field(L, t, "h", &pf.tr.h);
    comps[nc++] = "TransformComponent";
  }
  lua_pop(L, 1);

  lua_getfield(L, def, "Velocity");
  if (lua_istable(L, -1)) {
    int t = lua_gettop(L);
    pf.has_velocity = true;
    pf.vel.has_vx = get_num_field(L, t, "vx", &pf.vel.vx);
    pf.vel.has_vy = get_num_field(L, t, "vy", &pf.vel.vy);
    comps[nc++] = "VelocityComponent";
  }
  lua_pop(L, 1);

  lua_getfield(L, def, "Sprite");
  if (lua_istable(L, -1)) {
    int t = lua_gettop(L);
    pf.has_sprite = true;
    lua_getfield(L, t, "image");
    if (lua_isnumber(L, -1)) {
      pf.spr.image = (int)lua_tointeger(L, -1);
      pf.spr.has_image = true;
    } else if (lua_isstring(L, -1)) {
      int id = image_id_by_name(s->G, lua_tostring(L, -1));
      pf.spr.image = id < 0 ? 0 : id;     // unknown name -> first image
      pf.spr.has_image = true;
    }
    lua_pop(L, 1);
    comps[nc++] = "SpriteComponent";
  }
  lua_pop(L, 1);

  // Collision = {} -- presence opts the entity into collision detection.
  lua_getfield(L, def, "Collision");
  if (lua_istable(L, -1)) {
    pf.has_collision = true;
    comps[nc++] = "CollisionComponent";
  }
  lua_pop(L, 1);

  comps[nc] = NULL;

  if (nc == 0) {
    free(pf.name);
    return luaL_error(L, "prefab '%s' has no known components", name);
  }

  // Redefining an existing prefab (hot-reload friendly): drop the old archetype
  // and replace the entry in place, preserving nothing of the old spawn list.
  Prefab *existing = find_prefab(s, name);
  if (existing) {
    ECS_EntityFreeArchetype(existing->archetype);
    free(existing->name);
    free(existing->ents);
    *existing = pf;
    existing->archetype = ECS_EntityRegisterArchetype(s->G->ecs, existing->name, comps);
    LOG_DEBUG("Redefined prefab '%s' (%d components)", name, nc);
    return 0;
  }

  pf.archetype = ECS_EntityRegisterArchetype(s->G->ecs, pf.name, comps);

  if (s->prefab_count == s->prefab_cap) {
    size_t ncap = s->prefab_cap ? s->prefab_cap * 2 : 8;
    Prefab *q = realloc(s->prefabs, ncap * sizeof(Prefab));
    if (!q) {
      ECS_EntityFreeArchetype(pf.archetype);
      free(pf.name);
      return luaL_error(L, "prefab '%s': out of memory", name);
    }
    s->prefabs = q;
    s->prefab_cap = ncap;
  }
  s->prefabs[s->prefab_count++] = pf;
  LOG_DEBUG("Registered prefab '%s' (%d components)", name, nc);
  return 0;
}

// prefab(name)[ {def} ]  or  prefab(name, {def}).
// `prefab "Name" { ... }` desugars to prefab("Name")({...}), so prefab(name)
// returns a closure that takes the definition table.
static int l_prefab(lua_State *L) {
  luaL_checkstring(L, 1);
  lua_pushvalue(L, 1);
  lua_pushcclosure(L, l_prefab_define, 1);
  if (lua_istable(L, 2)) {     // immediate two-arg form: call right away
    lua_pushvalue(L, 2);
    lua_call(L, 1, 0);
    return 0;
  }
  return 1;                    // curried form: return the closure
}

static int l_spawn(lua_State *L) {
  Script *s = get_script(L);
  const char *name = luaL_checkstring(L, 1);
  Prefab *p = find_prefab(s, name);
  if (!p) return luaL_error(L, "spawn: unknown prefab '%s'", name);

  Entity e = ECS_EntityNew(s->G->ecs, p->archetype);
  apply_prefab(s, p, e, false, 0, 0);
  prefab_track(p, e);
  lua_pushinteger(L, (lua_Integer)e);
  return 1;
}

static int l_spawn_at(lua_State *L) {
  Script *s = get_script(L);
  const char *name = luaL_checkstring(L, 1);
  float x = (float)luaL_checknumber(L, 2);
  float y = (float)luaL_checknumber(L, 3);
  Prefab *p = find_prefab(s, name);
  if (!p) return luaL_error(L, "spawn_at: unknown prefab '%s'", name);

  Entity e = ECS_EntityNew(s->G->ecs, p->archetype);
  apply_prefab(s, p, e, true, x, y);
  prefab_track(p, e);
  lua_pushinteger(L, (lua_Integer)e);
  return 1;
}

static int l_destroy(lua_State *L) {
  Script *s = get_script(L);
  Entity e = (Entity)luaL_checkinteger(L, 1);
  ECS_EntityDelete(s->G->ecs, e);   // idempotent; no existence gate
  prefab_untrack(s, e);
  return 0;
}

// count(prefab) -- number of live entities spawned from this prefab. Prunes
// entities that no longer exist before counting.
static int l_count(lua_State *L) {
  Script *s = get_script(L);
  Prefab *p = find_prefab(s, luaL_checkstring(L, 1));
  if (!p) { lua_pushinteger(L, 0); return 1; }

  size_t n = 0;
  for (size_t i = 0; i < p->ent_count; i++)
    if (ECS_EntityExists(s->G->ecs, p->ents[i])) p->ents[n++] = p->ents[i];
  p->ent_count = n;

  lua_pushinteger(L, (lua_Integer)n);
  return 1;
}

// spawn_many(prefab, n) -- bulk-spawn n entities (no ids returned). Cheap way to
// add load (the benchmark); entities are tracked in C for despawn()/count().
static int l_spawn_many(lua_State *L) {
  Script *s = get_script(L);
  const char *name = luaL_checkstring(L, 1);
  lua_Integer n = luaL_checkinteger(L, 2);
  Prefab *p = find_prefab(s, name);
  if (!p) return luaL_error(L, "spawn_many: unknown prefab '%s'", name);
  for (lua_Integer i = 0; i < n; i++) {
    Entity e = ECS_EntityNew(s->G->ecs, p->archetype);
    apply_prefab(s, p, e, false, 0, 0);
    prefab_track(p, e);
  }
  return 0;
}

// despawn(prefab, n) -> removed. Destroy up to n of the prefab's entities
// (most-recently spawned first).
static int l_despawn(lua_State *L) {
  Script *s = get_script(L);
  const char *name = luaL_checkstring(L, 1);
  lua_Integer n = luaL_checkinteger(L, 2);
  Prefab *p = find_prefab(s, name);
  if (!p) { lua_pushinteger(L, 0); return 1; }
  lua_Integer removed = 0;
  while (n-- > 0 && p->ent_count > 0) {
    Entity e = p->ents[--p->ent_count];
    ECS_EntityDelete(s->G->ecs, e);   // idempotent; never gate on ECS_EntityExists
    removed++;
  }
  lua_pushinteger(L, removed);
  return 1;
}

// set_pos(e, x, y) -- move an entity (writes its TransformComponent).
static int l_set_pos(lua_State *L) {
  Script *s = get_script(L);
  Entity e = (Entity)luaL_checkinteger(L, 1);
  float x = (float)luaL_checknumber(L, 2);
  float y = (float)luaL_checknumber(L, 3);
  TransformComponent *t = ECS_EntityGetComponent(s->G->ecs, e, COMPONENT_ID(TransformComponent));
  if (t) { t->x = x; t->y = y; }
  return 0;
}

// get_pos(e) -> x, y  (nil if the entity has no Transform).
static int l_get_pos(lua_State *L) {
  Script *s = get_script(L);
  Entity e = (Entity)luaL_checkinteger(L, 1);
  TransformComponent *t = ECS_EntityGetComponent(s->G->ecs, e, COMPONENT_ID(TransformComponent));
  if (!t) { lua_pushnil(L); return 1; }
  lua_pushnumber(L, t->x);
  lua_pushnumber(L, t->y);
  return 2;
}

// fps() -> current smoothed frame rate.
static int l_fps(lua_State *L) {
  Script *s = get_script(L);
  lua_pushnumber(L, (lua_Number)s->G->frameRate);
  return 1;
}

// hud(text) -- set the on-screen status line (top-left in the level).
static int l_hud(lua_State *L) {
  Script *s = get_script(L);
  const char *txt = luaL_optstring(L, 1, "");
  snprintf(s->G->hud_text, sizeof(s->G->hud_text), "%s", txt);
  return 0;
}

// Map a friendly lowercase key name (as on_key receives) to an SDL scancode.
static SDL_Scancode scancode_from_name(const char *n) {
  if (!strcmp(n, "left"))  return SDL_SCANCODE_LEFT;
  if (!strcmp(n, "right")) return SDL_SCANCODE_RIGHT;
  if (!strcmp(n, "up"))    return SDL_SCANCODE_UP;
  if (!strcmp(n, "down"))  return SDL_SCANCODE_DOWN;
  if (!strcmp(n, "space")) return SDL_SCANCODE_SPACE;
  if (n[0] >= 'a' && n[0] <= 'z' && n[1] == '\0')
    return (SDL_Scancode)(SDL_SCANCODE_A + (n[0] - 'a'));
  return SDL_SCANCODE_UNKNOWN;
}

// key_down(name) -> bool. Live keyboard state, for held-key movement in
// on_update (on_key only fires on the discrete press/repeat events).
static int l_key_down(lua_State *L) {
  SDL_Scancode sc = scancode_from_name(luaL_checkstring(L, 1));
  const bool *state = SDL_GetKeyboardState(NULL);
  lua_pushboolean(L, sc != SDL_SCANCODE_UNKNOWN && state && state[sc]);
  return 1;
}

// time() -> seconds since start (for animations).
static int l_time(lua_State *L) {
  lua_pushnumber(L, (lua_Number)SDL_GetTicks() / 1000.0);
  return 1;
}

// scene() -> "menu" | "options" | "demos" | "video" | "play" | "benchmark" | "slots"
static int l_scene(lua_State *L) {
  struct Game *G = get_script(L)->G;
  const char *n = "menu";
  switch (G->state->sceneId) {
    case SCENE_MAIN_MENU_OPTIONS: n = "options"; break;
    case SCENE_MAIN_MENU_DEMOS:   n = "demos";   break;
    case SCENE_MAIN_MENU_VIDEO:   n = "video";   break;
    case SCENE_LEVEL:
      n = (G->state->mode == MODE_BENCHMARK) ? "benchmark"
        : (G->state->mode == MODE_SLOTS)     ? "slots"
        : (G->state->mode == MODE_CUBE)      ? "cube"
                                             : "play";
      break;
    default: n = "menu"; break;
  }
  lua_pushstring(L, n);
  return 1;
}

// goto_scene(name) -- navigate.
// name: "menu"|"options"|"demos"|"video"|"play"|"benchmark"|"slots"
static int l_goto_scene(lua_State *L) {
  struct Game *G = get_script(L)->G;
  const char *name = luaL_checkstring(L, 1);
  if      (!strcmp(name, "menu"))      G->state->sceneId = SCENE_MAIN_MENU;
  else if (!strcmp(name, "options"))   G->state->sceneId = SCENE_MAIN_MENU_OPTIONS;
  else if (!strcmp(name, "demos"))     G->state->sceneId = SCENE_MAIN_MENU_DEMOS;
  else if (!strcmp(name, "video"))     G->state->sceneId = SCENE_MAIN_MENU_VIDEO;
  else if (!strcmp(name, "play"))      { G->state->mode = MODE_INVADERS;  G->state->sceneId = SCENE_LEVEL; }
  else if (!strcmp(name, "benchmark")) { G->state->mode = MODE_BENCHMARK; G->state->sceneId = SCENE_LEVEL; }
  else if (!strcmp(name, "slots"))     { G->state->mode = MODE_SLOTS;     G->state->sceneId = SCENE_LEVEL; }
  else if (!strcmp(name, "cube"))      { G->state->mode = MODE_CUBE;      G->state->sceneId = SCENE_LEVEL; }
  else return luaL_error(L, "goto_scene: unknown scene '%s'", name);
  return 0;
}

// quit() -- exit the game.
static int l_quit(lua_State *L) {
  get_script(L)->G->is_running = false;
  return 0;
}

// --- Touch (logical coords) -------------------------------------------------
// touch_count() -> number of active touch points.
static int l_touch_count(lua_State *L) {
  struct Game *G = get_script(L)->G;
  int n = 0;
  for (int i = 0; i < MAX_TOUCHES; i++) if (G->touches[i].active) n++;
  lua_pushinteger(L, n);
  return 1;
}

// touch_pos(i) -> x, y of the i-th active touch (1-based), or nil if none.
static int l_touch_pos(lua_State *L) {
  struct Game *G = get_script(L)->G;
  int want = (int)luaL_checkinteger(L, 1);
  int n = 0;
  for (int i = 0; i < MAX_TOUCHES; i++) {
    if (!G->touches[i].active) continue;
    if (++n == want) {
      lua_pushnumber(L, G->touches[i].x);
      lua_pushnumber(L, G->touches[i].y);
      return 2;
    }
  }
  return 0;
}

// --- Video settings ---------------------------------------------------------
// set_fullscreen(on) -- toggle borderless desktop fullscreen.
static int l_set_fullscreen(lua_State *L) {
  struct Game *G = get_script(L)->G;
  SDL_SetWindowFullscreen(G->window, lua_toboolean(L, 1));
  return 0;
}

// is_fullscreen() -> bool.
static int l_is_fullscreen(lua_State *L) {
  struct Game *G = get_script(L)->G;
  lua_pushboolean(L, (SDL_GetWindowFlags(G->window) & SDL_WINDOW_FULLSCREEN) != 0);
  return 1;
}

// set_window_size(w, h) -- leave fullscreen, resize and re-center the window.
// The world is unaffected (logical presentation rescales it to the new size).
static int l_set_window_size(lua_State *L) {
  struct Game *G = get_script(L)->G;
  int w = (int)luaL_checkinteger(L, 1);
  int h = (int)luaL_checkinteger(L, 2);
  SDL_SetWindowFullscreen(G->window, false);
  SDL_SetWindowSize(G->window, w, h);
  SDL_SetWindowPosition(G->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  return 0;
}

// get_window_size() -> w, h of the current window (not the logical size).
static int l_get_window_size(lua_State *L) {
  struct Game *G = get_script(L)->G;
  int w, h;
  SDL_GetWindowSize(G->window, &w, &h);
  lua_pushinteger(L, w);
  lua_pushinteger(L, h);
  return 2;
}

static void register_api(lua_State *L) {
  static const luaL_Reg fns[] = {
    {"log",        l_log},
    {"prefab",     l_prefab},
    {"spawn",      l_spawn},
    {"spawn_at",   l_spawn_at},
    {"spawn_many", l_spawn_many},
    {"destroy",    l_destroy},
    {"despawn",    l_despawn},
    {"count",      l_count},
    {"set_pos",    l_set_pos},
    {"get_pos",    l_get_pos},
    {"key_down",   l_key_down},
    {"fps",        l_fps},
    {"hud",        l_hud},
    {"time",       l_time},
    {"scene",      l_scene},
    {"goto_scene", l_goto_scene},
    {"quit",       l_quit},
    {"touch_count",     l_touch_count},
    {"touch_pos",       l_touch_pos},
    {"set_fullscreen",  l_set_fullscreen},
    {"is_fullscreen",   l_is_fullscreen},
    {"set_window_size", l_set_window_size},
    {"get_window_size", l_get_window_size},
    {NULL, NULL}
  };
  for (const luaL_Reg *r = fns; r->name; r++) {
    lua_pushcfunction(L, r->func);
    lua_setglobal(L, r->name);
  }
  // Screen size for scripts. SCREEN_H is fixed; SCREEN_W follows the window
  // aspect (the adaptive logical width), so scenes fill the screen at any size.
  // Seeded from the current size here and refreshed on resize (see
  // script_update_screen_dims). Globals, not constants -- scripts re-read them.
  lua_pushinteger(L, g_logical_w); lua_setglobal(L, "SCREEN_W");
  lua_pushinteger(L, g_logical_h); lua_setglobal(L, "SCREEN_H");

  // Platform flag so scripts can pick safe limits on the web build (single
  // threaded, WebGL): the benchmark caps its load far lower there.
#ifdef __EMSCRIPTEN__
  lua_pushboolean(L, 1);
#else
  lua_pushboolean(L, 0);
#endif
  lua_setglobal(L, "IS_WEB");

  // Immediate-mode UI toolkit (the `ui` table).
  ui_lua_register(L);
  // Software 3D primitives (the `g3d` table).
  g3d_register(L);
}

// ---------------------------------------------------------------------------
// Engine prelude (Lua)
//
// Loaded before the user script. Provides the coroutine-based wave director:
//
//   start(fn, ...)  -- launch fn as a coroutine; its first segment runs now
//   wait(sec)       -- inside a coroutine: pause it for `sec` seconds
//
// C drives it once per frame via __tick(dt). Coroutines are NOT a hot path
// (a handful of wave directors), so the scheduler lives in Lua: readable, and
// it only ever calls the C spawn/destroy functions, never the inner loop.
// ---------------------------------------------------------------------------
static const char *ENGINE_PRELUDE =
  // Let scripts require sibling modules (e.g. a scene's layout file) from the
  // scripts/ directory: require('menu_view') -> scripts/menu_view.lua.
  "package.path = 'scripts/?.lua;' .. package.path\n"
  "local running = {}\n"                     // { {co=thread, timer=seconds}, ... }
  "function wait(sec)\n"
  "  coroutine.yield(sec or 0)\n"
  "end\n"
  // style(a, b, ...) -- merge style tables left-to-right (CSS-like cascade):
  // later tables override earlier keys. Returns a new table.
  "function style(...)\n"
  "  local out, args = {}, {...}\n"
  "  for i = 1, #args do\n"
  "    if args[i] then for k, v in pairs(args[i]) do out[k] = v end end\n"
  "  end\n"
  "  return out\n"
  "end\n"
  "function start(fn, ...)\n"
  "  local co = coroutine.create(fn)\n"
  "  local ok, res = coroutine.resume(co, ...)\n"       // run first segment now
  "  if not ok then\n"
  "    log('coroutine error: ' .. tostring(res))\n"
  "    return co\n"
  "  end\n"
  "  if coroutine.status(co) ~= 'dead' then\n"
  "    running[#running + 1] = { co = co, timer = tonumber(res) or 0 }\n"
  "  end\n"
  "  return co\n"
  "end\n"
  "function __tick(dt)\n"
  "  local i = 1\n"
  "  while i <= #running do\n"
  "    local c = running[i]\n"
  "    c.timer = c.timer - dt\n"
  "    if c.timer <= 0 then\n"
  "      local ok, res = coroutine.resume(c.co)\n"
  "      if not ok then\n"
  "        log('coroutine error: ' .. tostring(res))\n"
  "        table.remove(running, i)\n"
  "      elseif coroutine.status(c.co) == 'dead' then\n"
  "        table.remove(running, i)\n"
  "      else\n"
  "        c.timer = tonumber(res) or 0\n"
  "        i = i + 1\n"
  "      end\n"
  "    else\n"
  "      i = i + 1\n"
  "    end\n"
  "  end\n"
  "end\n"
  // -------------------------------------------------------------------------
  // Declarative view runtime
  //
  // mount(spec) turns a layout tree + stylesheet (spec.tree / spec.styles)
  // into a view. The scene binds button ids with view:on(id, fn) and emits
  // the tree each frame with view:render(); the walker below maps each node
  // to the immediate-mode ui.* toolkit. This keeps structure/look (the layout
  // file) separate from behavior (the scene). Lua-side: the menu is not a hot
  // path, and it only ever calls the same ui.* C functions a handful of times.
  // -------------------------------------------------------------------------
  "local function merge(...)\n"
  "  local out = {}\n"
  "  for i = 1, select('#', ...) do\n"
  "    local t = select(i, ...)\n"
  "    if t then for k, v in pairs(t) do out[k] = v end end\n"
  "  end\n"
  "  return out\n"
  "end\n"
  "local View = {}\n"
  "View.__index = View\n"
  "function View:on(id, fn) self.handlers[id] = fn return self end\n"
  // A node's final style: stylesheet[class] cascaded with any inline `style`.
  "local function resolve(self, n)\n"
  "  local base = n.class and self.styles[n.class] or nil\n"
  "  if n.style then return merge(base, n.style) end\n"
  "  return base or {}\n"
  "end\n"
  // Emit one node (and its children) into the current Clay context.
  "local function draw(self, n)\n"
  "  local tag = n.tag\n"
  "  local st = resolve(self, n)\n"
  "  if tag == 'panel' or tag == 'list' then\n"
  "    ui.panel(st, function()\n"
  "      for _, c in ipairs(n.children or {}) do draw(self, c) end\n"
  "    end)\n"
  "  elseif tag == 'label' then\n"
  "    ui.label(n.text or '', st)\n"
  "  elseif tag == 'button' then\n"
  // The keyboard cursor highlights its button by bolding it (font 1), matching
  // the mouse-hover look so both input methods read the same.
  "    if self.cursor and self.buttons[self.cursor] == n.id then\n"
  "      st = merge(st, { font = 1 })\n"
  "    end\n"
  "    if ui.button(n.id or 'btn', n.text or '', st) then\n"
  "      local fn = n.id and self.handlers[n.id]\n"
  "      if fn then fn() end\n"
  "    end\n"
  "  elseif tag == 'image' then\n"
  "    ui.image(n.image or 0, n.x or 0, n.y or 0, n.w or 0, n.h or 0)\n"
  "  elseif tag == 'text' then\n"
  "    ui.text(n.x or 0, n.y or 0, n.text or '', st)\n"
  "  elseif tag == 'rect' then\n"
  "    ui.rect(n.x or 0, n.y or 0, n.w or 0, n.h or 0, st)\n"
  "  end\n"
  "end\n"
  "function View:render()\n"
  "  for _, n in ipairs(self.tree) do draw(self, n) end\n"
  "end\n"
  // Keyboard navigation over the view's buttons (collected in tree order at
  // mount). nav() moves the cursor with wraparound; activate() fires the
  // handler under the cursor (Enter). Mouse clicks still work independently.
  "function View:nav(d)\n"
  "  local n = #self.buttons\n"
  "  if n == 0 then return end\n"
  "  self.cursor = (self.cursor - 1 + d) % n + 1\n"
  "end\n"
  "function View:activate()\n"
  "  local id = self.buttons[self.cursor]\n"
  "  local fn = id and self.handlers[id]\n"
  "  if fn then fn() end\n"
  "end\n"
  "local function collect_buttons(n, out)\n"
  "  if n.tag == 'button' and n.id then out[#out + 1] = n.id end\n"
  "  for _, c in ipairs(n.children or {}) do collect_buttons(c, out) end\n"
  "end\n"
  "function mount(spec)\n"
  "  local self = setmetatable({ tree = spec.tree or {}, styles = spec.styles or {},\n"
  "    handlers = {}, buttons = {}, cursor = 1 }, View)\n"
  "  for _, n in ipairs(self.tree) do collect_buttons(n, self.buttons) end\n"
  "  return self\n"
  "end\n";

// ---------------------------------------------------------------------------
// Callback dispatch helpers
// ---------------------------------------------------------------------------

static bool push_global_fn(lua_State *L, const char *name) {
  lua_getglobal(L, name);
  if (lua_isfunction(L, -1)) return true;
  lua_pop(L, 1);
  return false;
}

static void report_error(lua_State *L, const char *where) {
  LOG_ERROR("Lua error in %s: %s", where, lua_tostring(L, -1));
  lua_pop(L, 1);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Delete every entity this Script has spawned (used when reloading: the scene
// is rebuilt from scratch). The ECS must still be alive.
static void destroy_tracked_entities(Script *s) {
  for (size_t pi = 0; pi < s->prefab_count; pi++) {
    Prefab *p = &s->prefabs[pi];
    for (size_t i = 0; i < p->ent_count; i++)
      ECS_EntityDelete(s->G->ecs, p->ents[i]);  // idempotent; no existence gate
  }
}

// Free a Script's prefabs/archetypes, close its Lua state, free the struct.
// Does NOT delete spawned entities (on shutdown the ECS owns them; on reload
// the caller deletes them explicitly while the ECS is still alive).
static void free_script_struct(Script *s) {
  if (!s) return;
  for (size_t i = 0; i < s->prefab_count; i++) {
    ECS_EntityFreeArchetype(s->prefabs[i].archetype);
    free(s->prefabs[i].name);
    free(s->prefabs[i].ents);
  }
  free(s->prefabs);
  if (s->L) lua_close(s->L);
  free(s->path);
  free(s);
}

// Build a fresh, fully-initialized Script: new Lua state, API and engine
// prelude. If `path` is non-NULL it also runs that user script and its
// on_start(); a NULL path yields an idle state (no gameplay loaded). Returns
// NULL on any load error (after cleaning up everything it created, including
// any entities on_start spawned before failing).
static Script *script_boot(struct Game *G, const char *path) {
  Script *s = calloc(1, sizeof(*s));
  if (!s) {
    LOG_ERROR("Failed to allocate Script");
    return NULL;
  }
  s->G = G;
  s->path = path ? str_dup(path) : NULL;

  s->L = luaL_newstate();
  if (!s->L) {
    LOG_ERROR("luaL_newstate failed (out of memory)");
    free(s->path);
    free(s);
    return NULL;
  }
  *(Script **)lua_getextraspace(s->L) = s;
  luaL_openlibs(s->L);
  register_api(s->L);

  bool ok = true;
  if (luaL_dostring(s->L, ENGINE_PRELUDE) != LUA_OK) {
    report_error(s->L, "engine prelude");
    ok = false;
  } else if (path) {
    if (luaL_dofile(s->L, path) != LUA_OK) {
      report_error(s->L, "loading script");
      ok = false;
    } else if (push_global_fn(s->L, "on_start")) {
      if (lua_pcall(s->L, 0, 0, 0) != LUA_OK) {
        report_error(s->L, "on_start");
        ok = false;
      }
    }
  }

  if (!ok) {
    destroy_tracked_entities(s); // on_start may have spawned before erroring
    free_script_struct(s);
    return NULL;
  }

  if (path) {
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info)) s->last_mtime = info.modify_time;
  }
  return s;
}

// Boot a fresh state for `path` (may be NULL) and, only on success, tear down
// the old scene/state and swap the fresh one in. On failure the current script
// keeps running unchanged.
static bool script_swap(struct Game *G, const char *path) {
  Script *fresh = script_boot(G, path);
  if (!fresh) return false;
  if (G->script) {
    destroy_tracked_entities(G->script);
    free_script_struct(G->script);
  }
  G->script = fresh;
  return true;
}

bool script_init(struct Game *G) {
  // Boot an idle state (prelude + API, no gameplay). Modes load their script
  // on demand via script_load().
  if (!script_swap(G, NULL)) return false;
  LOG_DEBUG("Lua scripting layer initialized (idle)");
  return true;
}

bool script_load(struct Game *G, const char *path) {
  // A scene change starts from a clean slate: tear down the old script and WIPE
  // EVERY entity (not just the ones it tracked) before the new scene spawns, so
  // nothing leaks between scenes (e.g. benchmark sprites into the level).
  if (G->script) {
    free_script_struct(G->script);
    G->script = NULL;
  }
  ECS_DeleteAllEntities(G->ecs);

  // The HUD status line is sticky (a script sets it once and it persists until
  // overwritten). Clear it on a scene change so a line left by the previous
  // scene (e.g. the benchmark's "ramping up...") doesn't bleed into the next.
  G->hud_text[0] = '\0';

  Script *fresh = script_boot(G, path);
  if (!fresh) {
    LOG_ERROR("Failed to load script: %s", path);
    return false;
  }
  G->script = fresh;
  LOG_INFO("Loaded script: %s", path);
  return true;
}

const char *script_current_path(struct Game *G) {
  return (G->script) ? G->script->path : NULL;
}

void script_unload(struct Game *G) {
  script_swap(G, NULL);   // idle state; keeps running on the (unlikely) failure
  G->hud_text[0] = '\0';
}

// Rebuild the script world from its current file. The new state is fully loaded
// first; only if it loads cleanly are the old entities/state torn down. On
// failure the running game keeps going unchanged.
bool script_reload(struct Game *G) {
  if (!G->script || !G->script->path) return false;
  char *p = str_dup(G->script->path);
  bool ok = script_swap(G, p);
  if (ok) LOG_INFO("Scripts reloaded (%s)", p);
  else    LOG_ERROR("Script reload failed; keeping the current script");
  free(p);
  return ok;
}

// Poll the script file's mtime (throttled) and reload when it changes.
void script_check_reload(struct Game *G) {
  if (!G->script || !G->script->path) return;
  Script *s = G->script;

  s->watch_accum += (double)G->dtime;
  if (s->watch_accum < 0.5) return;
  s->watch_accum = 0.0;

  SDL_PathInfo info;
  if (!SDL_GetPathInfo(s->path, &info)) return;
  if (info.modify_time == s->last_mtime) return;

  // File changed. Reload, then record the seen mtime on whichever state is now
  // active (fresh on success, the still-running old one on failure) so a broken
  // file doesn't trigger a reload attempt every poll until it changes again.
  script_reload(G);
  G->script->last_mtime = info.modify_time;
}

void script_update(struct Game *G, float dt) {
  if (!G->script || !G->script->L) return;
  lua_State *L = G->script->L;

  // Advance the coroutine wave director first, then run game-level logic.
  if (push_global_fn(L, "__tick")) {
    lua_pushnumber(L, (lua_Number)dt);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) report_error(L, "__tick");
  }

  if (push_global_fn(L, "on_update")) {
    lua_pushnumber(L, (lua_Number)dt);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) report_error(L, "on_update");
  }
}

void script_update_screen_dims(struct Game *G) {
  if (!G || !G->script || !G->script->L) return;
  lua_State *L = G->script->L;
  lua_pushinteger(L, g_logical_w); lua_setglobal(L, "SCREEN_W");
  lua_pushinteger(L, g_logical_h); lua_setglobal(L, "SCREEN_H");
}

void script_on_collision(struct Game *G, Entity a, Entity b) {
  if (!G->script || !G->script->L) return;
  lua_State *L = G->script->L;

  if (push_global_fn(L, "on_collision")) {
    lua_pushinteger(L, (lua_Integer)a);
    lua_pushinteger(L, (lua_Integer)b);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) report_error(L, "on_collision");
  }
}

void script_on_key(struct Game *G, const char *key) {
  if (!G->script || !G->script->L) return;
  lua_State *L = G->script->L;

  if (push_global_fn(L, "on_key")) {
    lua_pushstring(L, key);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) report_error(L, "on_key");
  }
}

// Let the script draw its UI for this frame. Must run inside the Clay layout
// pass (the ui.* functions emit Clay elements).
void script_on_ui(struct Game *G) {
  if (!G->script || !G->script->L) return;
  lua_State *L = G->script->L;

  if (push_global_fn(L, "on_ui")) {
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) report_error(L, "on_ui");
  }
}

void script_free(struct Game *G) {
  if (!G->script) return;
  free_script_struct(G->script);
  G->script = NULL;
}
