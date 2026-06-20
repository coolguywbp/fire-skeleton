#include "ui_lua.h"

#include "game.h"    // Clay macros/types (via main.h), struct Game
#include "script.h"  // script_game (for ui.image -> G->images)

#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>   // atof

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

// Defined further down (layout helpers), used by the in-flow button too.
static Clay_Padding opt_padding(lua_State *L, int t, const char *key);
static void apply_border(lua_State *L, int t, Clay_ElementDeclaration *d);

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

// Floating (absolute) button: ui.button(id, x, y, w, h, label [, opts]).
static int ui_button_floating(lua_State *L, const char *id) {
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
  if (clicked) g_click = false;
  lua_pushboolean(L, clicked);
  return 1;
}

// In-flow button (inside an ui.panel): ui.button(id, label [, opts]). Text-only
// by default (bolds on hover, like the classic menu); set opts.color for a box.
static int ui_button_inflow(lua_State *L, const char *id) {
  const char *label = intern(luaL_checkstring(L, 2));
  int opt = lua_istable(L, 3) ? 3 : 0;

  Clay_Color bg  = opt_color(L, opt, "color",      (Clay_Color){0, 0, 0, 0});
  Clay_Color txt = opt_color(L, opt, "text_color", (Clay_Color){255, 255, 255, 255});
  uint16_t size  = (uint16_t)opt_num(L, opt, "size", 54);
  uint16_t font  = (uint16_t)opt_num(L, opt, "font", 0);

  Clay_String cid = clay_str(intern(id));
  bool over = Clay_PointerOver(Clay_GetElementId(cid));

  // Default padding 8 all round; opts.pad overrides (number or per-side table).
  Clay_Padding pad = (opt == 0) ? (Clay_Padding){ 8, 8, 8, 8 } : opt_padding(L, opt, "pad");
  if (opt && pad.left == 0 && pad.right == 0 && pad.top == 0 && pad.bottom == 0)
    pad = (Clay_Padding){ 8, 8, 8, 8 };

  Clay_ElementDeclaration d = {0};
  d.layout.padding = pad;
  d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
  d.backgroundColor = bg;
  d.cornerRadius = CLAY_CORNER_RADIUS((float)opt_num(L, opt, "radius", 0));
  apply_border(L, opt, &d);

  Clay__OpenElementWithId(Clay_GetElementId(cid));
  Clay__ConfigureOpenElement(d);
  Clay_TextElementConfig tc = {0};
  tc.fontSize = size;
  tc.textColor = txt;
  tc.fontId = over ? 1 : font;   // bold on hover
  Clay__OpenTextElement(clay_str(label), Clay__StoreTextElementConfig(tc));
  Clay__CloseElement();

  bool clicked = over && g_click;
  if (clicked) g_click = false;
  lua_pushboolean(L, clicked);
  return 1;
}

// ui.button dispatches by the 2nd argument: a string is the in-flow form, a
// number is the floating (absolute) form.
static int l_ui_button(lua_State *L) {
  const char *id = luaL_checkstring(L, 1);
  if (lua_type(L, 2) == LUA_TSTRING) return ui_button_inflow(L, id);
  return ui_button_floating(L, id);
}

// ---- layout containers (for menus) ----------------------------------------

// width/height value: a number (fixed px), "grow", "fit", or "NN%" (percent).
static Clay_SizingAxis size_axis(lua_State *L, int t, const char *key) {
  if (!t) return CLAY_SIZING_FIT(0);
  lua_getfield(L, t, key);
  Clay_SizingAxis ax = CLAY_SIZING_FIT(0);
  if (lua_isnumber(L, -1)) {
    ax = CLAY_SIZING_FIXED((float)lua_tonumber(L, -1));
  } else if (lua_isstring(L, -1)) {
    const char *s = lua_tostring(L, -1);
    if (!strcmp(s, "grow"))        ax = CLAY_SIZING_GROW(0);
    else if (strchr(s, '%'))       ax = CLAY_SIZING_PERCENT((float)atof(s) / 100.0f);
    else                           ax = CLAY_SIZING_FIT(0);
  }
  lua_pop(L, 1);
  return ax;
}

static const char *opt_str(lua_State *L, int t, const char *key, const char *def) {
  if (!t) return def;
  lua_getfield(L, t, key);
  const char *v = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
  lua_pop(L, 1);
  return v;
}

// Padding: a number (all sides), CSS positional shorthand {a} / {v,h} / {t,h,b}
// / {t,r,b,l}, or named {top=, right=, bottom=, left=}.
static Clay_Padding opt_padding(lua_State *L, int t, const char *key) {
  Clay_Padding p = {0};
  if (!t) return p;
  lua_getfield(L, t, key);
  if (lua_isnumber(L, -1)) {
    uint16_t a = (uint16_t)lua_tonumber(L, -1);
    p = (Clay_Padding){ a, a, a, a };
  } else if (lua_istable(L, -1)) {
    int tab = lua_gettop(L);
    bool named = false;
    const char *names[] = { "top", "right", "bottom", "left" };
    for (int i = 0; i < 4; i++) {
      lua_getfield(L, tab, names[i]);
      if (!lua_isnil(L, -1)) named = true;
      lua_pop(L, 1);
    }
    if (named) {
      p.top    = (uint16_t)opt_num(L, tab, "top", 0);
      p.right  = (uint16_t)opt_num(L, tab, "right", 0);
      p.bottom = (uint16_t)opt_num(L, tab, "bottom", 0);
      p.left   = (uint16_t)opt_num(L, tab, "left", 0);
    } else {
      int n = (int)lua_rawlen(L, tab);
      double v[4] = {0, 0, 0, 0};
      for (int i = 0; i < n && i < 4; i++) {
        lua_rawgeti(L, tab, i + 1);
        v[i] = lua_tonumber(L, -1);
        lua_pop(L, 1);
      }
      if (n <= 1)      p = (Clay_Padding){ (uint16_t)v[0], (uint16_t)v[0], (uint16_t)v[0], (uint16_t)v[0] };
      else if (n == 2) p = (Clay_Padding){ .left = (uint16_t)v[1], .right = (uint16_t)v[1], .top = (uint16_t)v[0], .bottom = (uint16_t)v[0] };
      else if (n == 3) p = (Clay_Padding){ .left = (uint16_t)v[1], .right = (uint16_t)v[1], .top = (uint16_t)v[0], .bottom = (uint16_t)v[2] };
      else             p = (Clay_Padding){ .left = (uint16_t)v[3], .right = (uint16_t)v[1], .top = (uint16_t)v[0], .bottom = (uint16_t)v[2] };
    }
  }
  lua_pop(L, 1);
  return p;
}

// Optional border: opts.border = { width=N, color={r,g,b,a} }.
static void apply_border(lua_State *L, int t, Clay_ElementDeclaration *d) {
  if (!t) return;
  lua_getfield(L, t, "border");
  if (lua_istable(L, -1)) {
    int b = lua_gettop(L);
    uint16_t w = (uint16_t)opt_num(L, b, "width", 0);
    Clay_Color col = opt_color(L, b, "color", (Clay_Color){255, 255, 255, 255});
    d->border.width = (Clay_BorderWidth){ .left = w, .right = w, .top = w, .bottom = w };
    d->border.color = col;
  }
  lua_pop(L, 1);
}

// ui.panel(opts, fn) -- a layout container; fn emits its children.
//   opts: dir="row"|"column", align_x/align_y="left|center|right"/"top|..",
//         gap, pad, width/height="grow"|number, color={...}, radius
static int l_ui_panel(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  int t = 1;

  const char *dir = opt_str(L, t, "dir", "column");
  const char *ax  = opt_str(L, t, "align_x", "left");
  const char *ay  = opt_str(L, t, "align_y", "top");

  Clay_ElementDeclaration d = {0};
  d.layout.layoutDirection = !strcmp(dir, "row") ? CLAY_LEFT_TO_RIGHT : CLAY_TOP_TO_BOTTOM;
  d.layout.childGap = (uint16_t)opt_num(L, t, "gap", 0);
  d.layout.padding = opt_padding(L, t, "pad");
  d.layout.childAlignment.x = !strcmp(ax, "center") ? CLAY_ALIGN_X_CENTER
                            : !strcmp(ax, "right")  ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT;
  d.layout.childAlignment.y = !strcmp(ay, "center") ? CLAY_ALIGN_Y_CENTER
                            : !strcmp(ay, "bottom") ? CLAY_ALIGN_Y_BOTTOM : CLAY_ALIGN_Y_TOP;
  d.layout.sizing.width  = size_axis(L, t, "width");
  d.layout.sizing.height = size_axis(L, t, "height");
  d.backgroundColor = opt_color(L, t, "color", (Clay_Color){0, 0, 0, 0});
  d.cornerRadius = CLAY_CORNER_RADIUS((float)opt_num(L, t, "radius", 0));
  apply_border(L, t, &d);

  Clay__OpenElement();
  Clay__ConfigureOpenElement(d);
  lua_pushvalue(L, 2);
  lua_call(L, 0, 0);          // emit children
  Clay__CloseElement();
  return 0;
}

// ui.label(text [, opts]) -- in-flow text (inside a panel).
static int l_ui_label(lua_State *L) {
  const char *str = intern(luaL_checkstring(L, 1));
  int opt = lua_istable(L, 2) ? 2 : 0;
  Clay_TextElementConfig tc = {0};
  tc.fontSize   = (uint16_t)opt_num(L, opt, "size", 24);
  tc.textColor  = opt_color(L, opt, "color", (Clay_Color){255, 255, 255, 255});
  tc.fontId     = (uint16_t)opt_num(L, opt, "font", 0);
  tc.lineHeight = (uint16_t)opt_num(L, opt, "line", 0);  // 0 = auto
  Clay__OpenTextElement(clay_str(str), Clay__StoreTextElementConfig(tc));
  return 0;
}

// ui.image(imageId, x, y, w, h) -- floating image overlay.
static int l_ui_image(lua_State *L) {
  int id = (int)luaL_checkinteger(L, 1);
  float x = (float)luaL_checknumber(L, 2);
  float y = (float)luaL_checknumber(L, 3);
  float w = (float)luaL_checknumber(L, 4);
  float h = (float)luaL_checknumber(L, 5);

  struct Game *G = script_game(L);
  if (!G || !G->images) return 0;

  Clay_ElementDeclaration d = {0};
  d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  d.floating.offset = (Clay_Vector2){ x, y };
  d.floating.zIndex = 750;
  d.floating.attachPoints.element = CLAY_ATTACH_POINT_LEFT_TOP;
  d.floating.attachPoints.parent  = CLAY_ATTACH_POINT_LEFT_TOP;
  d.layout.sizing.width  = CLAY_SIZING_FIXED(w);
  d.layout.sizing.height = CLAY_SIZING_FIXED(h);
  d.image.imageData = G->images[id];

  Clay__OpenElement();
  Clay__ConfigureOpenElement(d);
  Clay__CloseElement();
  return 0;
}

void ui_lua_register(lua_State *L) {
  static const luaL_Reg fns[] = {
    {"text",   l_ui_text},
    {"rect",   l_ui_rect},
    {"button", l_ui_button},
    {"panel",  l_ui_panel},
    {"label",  l_ui_label},
    {"image",  l_ui_image},
    {NULL, NULL}
  };
  luaL_newlib(L, fns);
  lua_setglobal(L, "ui");
}
