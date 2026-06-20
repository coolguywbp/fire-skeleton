#pragma once
#include <stdint.h>
#ifndef GAME_H
#define GAME_H

#include "main.h"
#include "ecs_core.h"
#include "renderer.h"

#include "utils.h"

typedef uint32_t ComponentMask;

typedef struct {
  SDL_Renderer *renderer;
  TTF_TextEngine *textEngine;
  TTF_Font **fonts;
  // Font file paths by fontId, so the renderer can open a dedicated handle per
  // (fontId, size) instead of resizing a shared font every frame.
  const char **font_paths;
} Clay_SDL3RendererData;

typedef struct{
  int x, y;
  int prevx, prevy;
  int debounceDelay;
  uint64_t lastClick;
} Mouse;

// Active touch points, in logical (1280x960) coordinates. Filled from SDL
// finger events and queried by scripts (touch_count/touch_pos) for the
// touch-driven demos. 10 covers any realistic multi-touch.
#define MAX_TOUCHES 10
typedef struct {
  SDL_FingerID id;
  float x, y;
  bool active;
} TouchPoint;

typedef struct{
  int sceneId;
  int mode;     // which gameplay script the level runs (see GameMode)
} GameState;

typedef struct Script Script;

struct Game {
  SDL_Window *window;
  SDL_Renderer *renderer;
  TTF_TextEngine *textEngine;
  Clay_SDL3RendererData *clayRendererData;
  
  SDL_Event event;
  Mouse *mouse;
  TouchPoint touches[MAX_TOUCHES];
  
  GameState *state;
  struct UI *ui;

  ECS *ecs;
  RenderCommandArray *renderCommands;
  
  EntityArchetype **archetypes;
  SDL_Texture **images;

  Script *script;

  // One-line status text a script can set via hud(); shown top-left in level.
  char hud_text[128];

  bool debug;
  bool is_running;

  float frameRate;
  float dtime;
  uint64_t lastFrameCounter;
  uint64_t lastFrameRateUpdate;

  // Windowed FPS averaging.
  double frameAccumTime;
  int frameAccumCount;
};

// The one canonical render size: the adaptive logical space everything draws
// into. Height is fixed (WINDOW_HEIGHT); width tracks the window's aspect ratio
// so the world fills the screen edge to edge with no letterbox bars. Computed
// by game_recompute_presentation on every window-size change; the renderer's
// logical presentation, Clay's layout dimensions and the Lua SCREEN_W/SCREEN_H
// globals are all derived from it. A global (not a Game field) so component
// constructors and ECS systems -- which run without a Game pointer -- read the
// same value. Defaults to the design size until the first presentation runs.
extern int g_logical_w, g_logical_h;

bool game_new(struct Game **game);
void game_free(struct Game **game);
bool game_run(struct Game *G);

// Recompute g_logical_w/h from the current window pixel size, then push it to
// the renderer's logical presentation, Clay's layout dimensions and the
// script's SCREEN_W/H globals. Safe before the UI/script exist (it skips
// whichever isn't ready yet).
void game_recompute_presentation(struct Game *G);

typedef enum {
  SCENE_NONE = 0,
  SCENE_MAIN_MENU,
  SCENE_MAIN_MENU_OPTIONS,
  SCENE_MAIN_MENU_DEMOS,
  SCENE_MAIN_MENU_VIDEO,
  SCENE_LEVEL,
  SCENE_SHOP,
  SCENE_MAX
} Scenes;

// Which script the level loads. Set by the demo-picker menu (Play opens it).
typedef enum {
  MODE_INVADERS = 0,
  MODE_BENCHMARK,
  MODE_SLOTS
} GameMode;

#endif
