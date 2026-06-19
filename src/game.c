#include "game.h"

#include "clay_renderer.h"
#include "components.h"
#include "init_clay.h"
#include "init_sdl.h"
#include "init_ecs.h"

#include "load_m.h"
#include "archetypes.h"
#include "benchmark.h"

#include "logger.h"
#include "ui.h"

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

void game_render_color(struct Game *G);
void game_toggle_music(void);
void game_toggle_debug(struct Game *G);
void game_events(struct Game *G);
void game_update(struct Game *G);
void game_render(struct Game *G);

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

  // calloc so x/y/prevx/prevy start at 0; uninitialized coords were causing
  // spurious "clicks" (Clay_PointerOver tested against garbage positions).
  // Allocated before the ECS so the velocity system can capture the mouse
  // pointer (used for cursor repulsion) at registration time.
  G->mouse = calloc(1, sizeof(Mouse));
  G->mouse->debounceDelay = 50;
  G->mouse->lastClick = 0;

  G->state = calloc(1, sizeof(GameState));
  G->state->sceneId = SCENE_MAIN_MENU;

  LOG_DEBUG("(Init 3/5) Started ECS init");
  if (!init_ecs(G)) {
    LOG_FATAL("ECS init failed");
    return false;
  } else{
    LOG_DEBUG("(Init 4/5) ECS OK");
  }

  G->is_running = true;
  G->debug = false;

  G->frameRate = 0;
  G->lastFrameRateUpdate = SDL_GetPerformanceCounter();

  srand((unsigned)time(NULL));

  // The benchmark owns entity spawning: it starts at 1 object on entering the
  // level and adapts the count to the frame rate.
  G->bench = benchmark_new();
  if (!G->bench) {
    fprintf(stderr, "Error allocating Benchmark.\n");
    return false;
  }

  return true;
}

void game_free(struct Game **game) {
  if (*game) {
    struct Game *G = *game;

    // ECS first: unregistering systems frees their archetypes and udata, and
    // entity deletion runs the component destructors.
    if (G->ecs){
      ECS_Delete(G->ecs);
      G->ecs = NULL;
    }

    benchmark_free(G->bench);
    G->bench = NULL;

    // Game-owned archetypes (registered separately from the system archetypes).
    if (G->archetypes) {
      for (int i = 0; i < MAX_ARCHETYPES; i++)
        ECS_EntityFreeArchetype(G->archetypes[i]);
      free(G->archetypes);
      G->archetypes = NULL;
    }

    // Image textures must be destroyed before the renderer that owns them.
    // (count must match load_images())
    if (G->images) {
      for (int i = 0; i < 2; i++)
        if (G->images[i]) SDL_DestroyTexture(G->images[i]);
      free(G->images);
      G->images = NULL;
    }

    // Clay/TTF renderer resources. (font count must match load_fonts())
    if (G->clayRendererData) {
      if (G->clayRendererData->fonts) {
        for (int i = 0; i < 2; i++)
          if (G->clayRendererData->fonts[i]) TTF_CloseFont(G->clayRendererData->fonts[i]);
        SDL_free(G->clayRendererData->fonts);
      }
      free(G->clayRendererData);
      G->clayRendererData = NULL;
    }
    // The text engine depends on the renderer, so destroy it first.
    if (G->textEngine) {
      TTF_DestroyRendererTextEngine(G->textEngine);
      G->textEngine = NULL;
    }

    free(G->ui);
    G->ui = NULL;
    free(G->mouse);
    G->mouse = NULL;
    free(G->state);
    G->state = NULL;

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
      // Always update on motion. The previous gate compared prev-vs-current
      // instead of event-vs-current, so a single duplicate-position event left
      // prevx==x && prevy==y and froze all further updates (Clay stopped
      // receiving the pointer position, killing hover and clicks) until a
      // window re-enter reset prev. Just forward every motion event.
      G->mouse->prevx = G->mouse->x;
      G->mouse->prevy = G->mouse->y;
      G->mouse->x = G->event.motion.x;
      G->mouse->y = G->event.motion.y;
      Clay_SetPointerState((Clay_Vector2) { G->mouse->x, G->mouse->y }, false);
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
  // The benchmark runs in the level: it starts fresh on entry and tears down
  // its entities on exit.
  if (G->state->sceneId == SCENE_LEVEL) {
    if (!G->bench->active) benchmark_start(G);
  } else {
    if (G->bench->active) benchmark_stop(G);
  }

  // Per-scene logic that must run before layout could go here. ECS updates run
  // in the render phase (see game_render) so the sprite render system draws
  // after SDL_RenderClear instead of being immediately erased.
  ui_update(G);
}

void game_render(struct Game *G) {
  SDL_RenderClear(G->renderer);

  // ECS render systems (e.g. SpriteRenderSystem) draw here, after the clear and
  // before the UI overlay. VelocitySystem logic also runs in this pass.
  // The sprite system queues quads into the batch; we flush them in one draw.
  if (G->state->sceneId == SCENE_LEVEL) {
    ECS_Update(G->ecs);
  }

  SDL_Clay_RenderClayCommands(G->clayRendererData, &G->ui->renderCommands);
  SDL_RenderPresent(G->renderer);
}

// One iteration of the game loop. Shared by the native loop and the
// browser (Emscripten) requestAnimationFrame callback.
static void game_frame(struct Game *G) {
  game_events(G);
  game_update(G);
  game_render(G);
  update_frame_rate(G);

  if (G->state->sceneId == SCENE_LEVEL) {
    // In the level the benchmark drives spawning; run uncapped on native.
    benchmark_update(G);
  } else {
#ifndef __EMSCRIPTEN__
    // Cap the menus at ~60 FPS to avoid spinning the CPU. On the web the
    // browser paces frames (requestAnimationFrame), so no manual delay.
    SDL_Delay(16);
#endif
  }
}

#ifdef __EMSCRIPTEN__
static void game_frame_em(void *arg) {
  struct Game *G = arg;
  game_frame(G);
  if (!G->is_running) emscripten_cancel_main_loop();
}
#endif

bool game_run(struct Game *G) {
#ifdef __EMSCRIPTEN__
  // WORK IN PROGRESS: the wasm build compiles but is not yet verified in a
  // browser (renderer/canvas, input, assets, benchmark pacing still need work).
  // The browser owns the loop; hand it our per-frame callback. fps=0 means
  // "use requestAnimationFrame"; the 1 unwinds the C stack so this call does
  // not return and G stays alive for the lifetime of the page.
  emscripten_set_main_loop_arg(game_frame_em, G, 0, 1);
#else
  while (G->is_running) {
    game_frame(G);
  }
#endif
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
  G->dtime = (float)timeInSeconds;
  G->lastFrameCounter = currentFrameCounter;

  // Windowed average: a stable, responsive reading instead of a noisy
  // single-frame instantaneous value.
  G->frameAccumTime += timeInSeconds;
  G->frameAccumCount++;
  if (G->frameAccumTime >= 0.25) {
    G->frameRate = (float)(G->frameAccumCount / G->frameAccumTime);
    G->frameAccumTime = 0;
    G->frameAccumCount = 0;
  }
}
