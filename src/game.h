#ifndef GAME_H
#define GAME_H

#include "main.h"

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

struct Game {
  SDL_Window *window;
  SDL_Renderer *renderer;
  TTF_TextEngine *textEngine;
  Clay_SDL3RendererData *clayRendererData;
  
  SDL_Event event;
  Mouse *mouse;
  
  GameState *state;
  struct UI *ui;

  SDL_Texture **images;
  
  bool debug;
  bool is_running;

  float frameRate;
  float dtime;
  uint64_t lastFrameCounter;
  uint64_t lastFrameRateUpdate;
};

bool game_new(struct Game **game);
void game_free(struct Game **game);
bool game_run(struct Game *G);

typedef enum {
  SCENE_NONE = 0,
  SCENE_MAIN_MENU,
  SCENE_MAIN_MENU_OPTIONS,
  SCENE_LEVEL_1,
  SCENE_SHOP,
  SCENE_MAX
} Scenes;

#endif
