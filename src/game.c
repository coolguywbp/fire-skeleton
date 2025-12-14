#include "game.h"

#include "clay_renderer.h"
#include "components.h"
#include "init_clay.h"
#include "init_sdl.h"
#include "init_ecs.h"

#include "load_m.h"
#include "archetypes.h"

#include "logger.h"
#include "ui.h"

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

void game_render_color(struct Game *G);
void game_toggle_music(void);
void game_toggle_debug(struct Game *G);
void game_events(struct Game *G);
void game_update(struct Game *G);
void game_render(const struct Game *G);

void update_frame_rate(struct Game *G);
void mouse_click_events(struct Game *G);

bool game_new(struct Game **game) {
  *game = calloc(1, sizeof(struct Game));
  if (*game == NULL) {
    fprintf(stderr, "Error Calloc of New Game.\n");
    return false;
  }
  
  LOG_DEBUG("Starting Game initialization");

  struct Game *G = *game;
  
  LOG_DEBUG("(Init 0/5) Started SDL init ");
  if (!game_init_sdl(G)) {
    return false;
  }else{
    LOG_DEBUG("(Init 1/5) SDL OK");
  }

  LOG_DEBUG("(Init 1/5) Started media loading");
  if (!game_load_media(G)) {
    return false;
  }else{
    LOG_DEBUG("(Init 2/5) Media OK");
  }

  LOG_DEBUG("(Init 2/5) Started Clay init");
  if (!ui_init_clay(G)) {
    return false;
  }else{
    LOG_DEBUG("(Init 3/5) Clay OK");
  }

  LOG_DEBUG("(Init 3/5) Started ECS init");
  if (!init_ecs(G)) {
    LOG_FATAL("ECS init failed");
    return false;
  } else{
    LOG_DEBUG("(Init 4/5) ECS OK");
  }
  
  G->mouse = malloc(sizeof(Mouse));
  G->mouse->debounceDelay = 50;
  G->mouse->lastClick = 0;
  
  G->state = malloc(sizeof(GameState));
  G->state->sceneId = SCENE_MAIN_MENU;
  

  G->is_running = true;
  G->debug = false;

  G->frameRate = 0;
  G->lastFrameRateUpdate = SDL_GetPerformanceCounter();

  srand((unsigned)time(NULL));


  Entity entity;
  TransformComponent *comp;
  entity = ECS_EntityNew(G->ecs, G->archetypes[TEST_BOUNCING_SPRITE_ARCHETYPE]);
  return true;
}

void game_free(struct Game **game) {
  if (*game) {
    struct Game *G = *game;

    if (G->renderer) {
      SDL_DestroyRenderer(G->renderer);
      G->renderer = NULL;
    }
    if (G->window) {
      SDL_DestroyWindow(G->window);
      G->window = NULL;
    }
    
    TTF_Quit();
    SDL_Quit();
  
    if (G->ecs){
      ECS_Delete(G->ecs);
      G->ecs = NULL;
    }

	  //free(test_sys);

    free(G);

    G = NULL;
    *game = NULL;

    printf("All Clean!\n");
  }
}

void game_events(struct Game *G) {
  while (SDL_PollEvent(&G->event)) {

    uint64_t currentTime = SDL_GetTicks();
    uint64_t clickDelta;

    switch (G->event.type) {
    case SDL_EVENT_QUIT:
      G->is_running = false;
      break;
    case SDL_EVENT_KEY_DOWN:
      switch (G->event.key.scancode) {
      case SDL_SCANCODE_Q:
      case SDL_SCANCODE_ESCAPE:
        G->state->sceneId = SCENE_MAIN_MENU;
        //G->is_running = false;
        break;
      case SDL_SCANCODE_SPACE:
        break;
      case SDL_SCANCODE_M:
        // game_toggle_music();
      case SDL_SCANCODE_E:
        game_toggle_debug(G);
        break;
      default:
        break;
      }
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      //DEBOUNING CLICKS ???
      clickDelta = currentTime - G->mouse->lastClick;
      double difference = clickDelta - G->mouse->debounceDelay;
      // printf("Delta: %f\n", (double)clickDelta);
      // printf("Debounce delay: %f\n", (double)G->mouse->debounceDelay);
      // printf("Difference: %f\n", (double)difference);
      if(difference > 0.0){
        Clay_SetPointerState((Clay_Vector2) { G->mouse->x, G->mouse->y }, true);
        G->mouse->lastClick = currentTime;
        mouse_click_events(G);
      };

      break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
      Clay_SetPointerState((Clay_Vector2) { G->mouse->x, G->mouse->y }, false); 
      break;
      
    case SDL_EVENT_MOUSE_MOTION:
      if (G->mouse->prevx != G->mouse->x || G->mouse->prevy != G->mouse->y){
        G->mouse->prevx = G->mouse->x;
        G->mouse->prevy = G->mouse->y;
        G->mouse->x = G->event.motion.x;
        G->mouse->y = G->event.motion.y;
        // printf("Mouse moved: (x %d,y %d)\n", G->mouse->x, G->mouse->y);
        Clay_SetPointerState((Clay_Vector2) { G->mouse->x, G->mouse->y }, false);
      }
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      Clay_UpdateScrollContainers(true, (Clay_Vector2) { G->event.wheel.x, G->event.wheel.x },G->dtime);
      break;
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
      // printf("Mouse entered window\n");
      // Reset previous position to current position
      // G->mouse->x = G->mouse->prevx;
      // G->mouse->y = G->mouse->prevy;
      G->mouse->prevx = -1;
      G->mouse->prevy = -1;
      break;

    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
      // printf("Mouse left window\n");
      // Optionally handle mouse leaving the window
      break;
    default:
      break;
    }
  }
}

void mouse_click_events(struct Game *G){
  ui_click_events(G);
}

void game_update(struct Game *G) {
  switch(G->state->sceneId){
    case SCENE_MAIN_MENU:
    case SCENE_MAIN_MENU_OPTIONS:
    break;

    case SCENE_LEVEL:
    ECS_Update(G->ecs);
    break;
    default:
    break;
  }
  ui_update(G);
}

void game_render(const struct Game *G) {
  SDL_RenderClear(G->renderer);
  //SDL_RenderCommands(G->renderer, G->renderCommands);
  SDL_Clay_RenderClayCommands(G->clayRendererData, &G->ui->renderCommands);
  SDL_RenderPresent(G->renderer);
}

bool game_run(struct Game *G) {
  while (G->is_running) {
    game_events(G);
    game_update(G);
    game_render(G);
    update_frame_rate(G);
    SDL_Delay(16);
  }
  return true;
}

void game_toggle_debug(struct Game *G){
  #ifdef DEV_MODE
  G->debug = !G->debug;
  Clay_SetDebugModeEnabled(G->debug);
  #endif /* ifdef DEV_MODE */
  }

  void update_frame_rate(struct Game *G){
  uint64_t currentFrameCounter = SDL_GetPerformanceCounter();
  double timeElapsed = (double)(currentFrameCounter - G->lastFrameCounter);
  double timeInSeconds = timeElapsed / (double)SDL_GetPerformanceFrequency();
  G->dtime = timeInSeconds;
  G->lastFrameCounter = currentFrameCounter;

  double lastFrameRateUpdateInSeconds = (currentFrameCounter - G->lastFrameRateUpdate) / (double)SDL_GetPerformanceFrequency();
  if (lastFrameRateUpdateInSeconds > 1){
    G->lastFrameRateUpdate = currentFrameCounter;
    G->frameRate = 1.0f / timeInSeconds;
  }
  // printf("FPS: %f \n", G->frameRate);
}
