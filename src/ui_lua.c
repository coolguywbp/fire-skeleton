#include "ui_lua.h"

#include "game.h"   // Clay macros/types (via main.h), struct Game

#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <stdio.h>

// Per-frame bump arena. Clay stores string pointers and only measures/renders
// them later in the frame, so every string handed to it must outlive the layout
// pass. We copy Lua strings here and reset once per frame; no malloc on the UI
// path. Also used to mint unique element ids for text/rect.
#define UI_ARENA 16384
static char   g_arena[UI_ARENA];
static size_t g_used;
static int    g_idseq;
static bool   g_click;   // a press happened this frame, not yet consumed

void ui_lua_begin_frame(void) { g_used = 0; g_idseq = 0; }
void ui_lua_end_frame(void)   { g_click = false; }
void ui_lua_note_click(void)  { g_click = true; }

static const char *intern(const char *s) {
  size_t n = strlen(s) + 1;
  if (g_used + n > UI_ARENA) return "";   // overflow: drop gracefully
  char *p = g_arena + g_used;
  memcpy(p, s, n);
  g_used += n;
  return p;
}

static Clay_String clay_str(const char *s) {
  return (Clay_String){ false, (int32_t)strlen(s), s };
}

// A unique interned element id for this frame ("ui0", "ui1", ...).
static Clay_String auto_id(void) {
  char buf[16];
  snprintf(buf, sizeof buf, "ui%d", g_idseq++);
  return clay_str(intern(buf));
}

// Read an optional numeric field from the opts table at stack index `t`
// (t == 0 means "no table").
static lua_Number opt_num(lua_State *L, int t, const char *key, lua_Number def) {
  if (t == 0) return def;
  lua_getfield(L, t, key);
  lua_Number v = luaL_optnumber(L, -1, def);
  lua_pop(L, 1);
  return v;
}

// Read an optional {r,g,b,a} color field (0-255). Missing alpha defaults to 255.
static Clay_Color opt_color(lua_State *L, int t, const char *key, Clay_Color def) {
  if (t == 0) return def;
  lua_getfield(L, t, key);
  if (!lua_istable(L, -1)) { lua_pop(L, 1); return def; }
  int c = lua_gettop(L);
  Clay_Color out;
  lua_rawgeti(L, c, 1); out.r = (float)luaL_optnumber(L, -1, 0);   lua_pop(L, 1);
  lua_rawgeti(L, c, 2); out.g = (float)luaL_optnumber(L, -1, 0);   lua_pop(L, 1);
  lua_rawgeti(L, c, 3); out.b = (float)luaL_optnumber(L, -1, 0);   lua_pop(L, 1);
  lua_rawgeti(L, c, 4); out.a = (float)luaL_optnumber(L, -1, 255); lua_pop(L, 1);
  lua_pop(L, 1);
  return out;
}

// ui.text(x, y, str [, opts])
static int l_ui_text(lua_State *L) {
  float x = (float)luaL_checknumber(L, 1);
  float y = (float)luaL_checknumber(L, 2);
  const char *str = intern(luaL_checkstring(L, 3));
  int opt = lua_istable(L, 4) ? 4 : 0;

  Clay_Color col = opt_color(L, opt, "color", (Clay_Color){255, 255, 255, 255});
  uint16_t size  = (uint16_t)opt_num(L, opt, "size", 24);
  uint16_t font  = (uint16_t)opt_num(L, opt, "font", 0);

  CLAY(CLAY_SID(auto_id()), {
    .floating = { .offset = { x, y }, .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 900,
                  .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP,
                                    .parent  = CLAY_ATTACH_POINT_LEFT_TOP } }
  }) {
    CLAY_TEXT(clay_str(str), CLAY_TEXT_CONFIG({
      .fontSize = size, .textColor = col, .fontId = font }));
  }
  return 0;
}

// ui.rect(x, y, w, h [, opts])
static int l_ui_rect(lua_State *L) {
  float x = (float)luaL_checknumber(L, 1);
  float y = (float)luaL_checknumber(L, 2);
  float w = (float)luaL_checknumber(L, 3);
  float h = (float)luaL_checknumber(L, 4);
  int opt = lua_istable(L, 5) ? 5 : 0;

  Clay_Color col = opt_color(L, opt, "color", (Clay_Color){0, 0, 0, 180});
  float radius   = (float)opt_num(L, opt, "radius", 0);

  CLAY(CLAY_SID(auto_id()), {
    .floating = { .offset = { x, y }, .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 800,
                  .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP,
                                    .parent  = CLAY_ATTACH_POINT_LEFT_TOP } },
    .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h) } },
    .backgroundColor = col,
    .cornerRadius = CLAY_CORNER_RADIUS(radius)
  }) {}
  return 0;
}

// ui.button(id, x, y, w, h, label [, opts]) -> bool clicked
static int l_ui_button(lua_State *L) {
  const char *id = luaL_checkstring(L, 1);
  float x = (float)luaL_checknumber(L, 2);
  float y = (float)luaL_checknumber(L, 3);
  float w = (float)luaL_checknumber(L, 4);
  float h = (float)luaL_checknumber(L, 5);
  const char *label = intern(luaL_checkstring(L, 6));
  int opt = lua_istable(L, 7) ? 7 : 0;

  Clay_Color bg   = opt_color(L, opt, "color",       (Clay_Color){40, 40, 60, 255});
  Clay_Color bgh  = opt_color(L, opt, "hover_color", (Clay_Color){70, 70, 110, 255});
  Clay_Color txt  = opt_color(L, opt, "text_color",  (Clay_Color){255, 255, 255, 255});
  uint16_t   size = (uint16_t)opt_num(L, opt, "size", 40);

  Clay_String cid = clay_str(intern(id));
  bool over = Clay_PointerOver(Clay_GetElementId(cid));

  CLAY(CLAY_SID(cid), {
    .floating = { .offset = { x, y }, .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 850,
                  .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP,
                                    .parent  = CLAY_ATTACH_POINT_LEFT_TOP } },
    .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h) },
                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
    .backgroundColor = over ? bgh : bg,
    .cornerRadius = CLAY_CORNER_RADIUS(8)
  }) {
    CLAY_TEXT(clay_str(label), CLAY_TEXT_CONFIG({
      .fontSize = size, .textColor = txt, .fontId = 1 }));
  }

  bool clicked = over && g_click;
  if (clicked) g_click = false;   // consume so only one button fires per press
  lua_pushboolean(L, clicked);
  return 1;
}

void ui_lua_register(lua_State *L) {
  static const luaL_Reg fns[] = {
    {"text",   l_ui_text},
    {"rect",   l_ui_rect},
    {"button", l_ui_button},
    {NULL, NULL}
  };
  luaL_newlib(L, fns);
  lua_setglobal(L, "ui");
}
