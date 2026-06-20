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
} Clay_SDL3RendererData;

typedef struct{
  int x, y;
  int prevx, prevy;
  int debounceDelay;
  uint64_t lastClick;
} Mouse;

typedef struct{
  int sceneId;
} GameState;

typedef struct Benchmark Benchmark;
typedef struct Script Script;

struct Game {
  SDL_Window *window;
  SDL_Renderer *renderer;
  TTF_TextEngine *textEngine;
  Clay_SDL3RendererData *clayRendererData;
  
  SDL_Event event;
  Mouse *mouse;
  
  GameState *state;
  struct UI *ui;
  
  ECS *ecs;
  RenderCommandArray *renderCommands;
  
  EntityArchetype **archetypes;
  SDL_Texture **images;

  Benchmark *bench;
  Script *script;

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

bool game_new(struct Game **game);
void game_free(struct Game **game);
bool game_run(struct Game *G);

typedef enum {
  SCENE_NONE = 0,
  SCENE_MAIN_MENU,
  SCENE_MAIN_MENU_OPTIONS,
  SCENE_LEVEL,
  SCENE_SHOP,
  SCENE_MAX
} Scenes;

#endif
