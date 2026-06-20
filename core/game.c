#include "game.h"

#include "clay_renderer.h"
#include "components.h"
#include "init_clay.h"
#include "init_sdl.h"
#include "init_ecs.h"

#include "load_m.h"
#include "load_i.h"
#include "archetypes.h"

#include <string.h>

#include "logger.h"
#include "script.h"
#include "collision.h"
#include "ecs_entity.h"
#include "ui.h"
#include "ui_lua.h"
#include "g3d.h"

#include <ctype.h>

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

// Keep the SDL window (and the canvas backing store) matched to the browser
// viewport, so the game fills the whole screen on desktop and mobile. The
// resize event then rebuilds the adaptive logical space to the new aspect
// (game_recompute_presentation), so the world fills the canvas with no bars.
static EM_BOOL on_web_resize(int type, const EmscriptenUiEvent *e, void *ud) {
  (void)type;
  struct Game *G = ud;
  if (e->windowInnerWidth > 0 && e->windowInnerHeight > 0)
    SDL_SetWindowSize(G->window, e->windowInnerWidth, e->windowInnerHeight);
  return EM_TRUE;
}

static void web_fit_canvas(struct Game *G) {
  int vw = EM_ASM_INT({ return window.innerWidth;  });
  int vh = EM_ASM_INT({ return window.innerHeight; });
  if (vw > 0 && vh > 0) SDL_SetWindowSize(G->window, vw, vh);
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, G, EM_FALSE,
                                 on_web_resize);
}
#endif

void game_render_color(struct Game *G);
void game_toggle_music(void);
void game_toggle_debug(struct Game *G);
void game_events(struct Game *G);
void game_update(struct Game *G);
void game_render(struct Game *G);

void update_frame_rate(struct Game *G);
void mouse_click_events(struct Game *G);
static void dispatch_key_to_script(struct Game *G, SDL_Scancode sc);

// Map window coordinates to the renderer's logical space, so input lands on the
// right spot regardless of window size (the logical space adapts to the aspect).
static void window_to_logical(struct Game *G, float wx, float wy,
                              float *lx, float *ly) {
  if (!SDL_RenderCoordinatesFromWindow(G->renderer, wx, wy, lx, ly)) {
    *lx = wx; *ly = wy;   // fall back to raw coords if the query fails
  }
}

// Record/refresh/drop an active touch (logical coords), keyed by finger id.
static void touch_set(struct Game *G, SDL_FingerID id, float x, float y, bool down) {
  int free_slot = -1;
  for (int i = 0; i < MAX_TOUCHES; i++) {
    if (G->touches[i].active && G->touches[i].id == id) {
      if (down) { G->touches[i].x = x; G->touches[i].y = y; }
      else      { G->touches[i].active = false; }
      return;
    }
    if (free_slot < 0 && !G->touches[i].active) free_slot = i;
  }
  if (down && free_slot >= 0)
    G->touches[free_slot] = (TouchPoint){ .id = id, .x = x, .y = y, .active = true };
}

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
  // Seed BOTH frame timestamps. lastFrameCounter drives dt/frameRate in
  // update_frame_rate; leaving it at 0 (calloc) made the first frame's dt the
  // entire perf-counter value -- on the web that's an epoch-based number, i.e.
  // ~1.8e9 seconds -- which poisoned the first FPS sample (smoothed ~0) and gave
  // the benchmark a bogus instant "peak" and a garbage on-screen FPS reading.
  G->lastFrameRateUpdate = SDL_GetPerformanceCounter();
  G->lastFrameCounter = SDL_GetPerformanceCounter();

  srand((unsigned)time(NULL));

  // Lua scripting layer (idle state). Gameplay scripts are loaded on demand when
  // a mode is entered from the menu. Non-fatal if it fails to initialize.
  if (!script_init(G)) {
    LOG_ERROR("Scripting layer failed to initialize; continuing without it");
  } else {
    LOG_DEBUG("(Init 5/5) Scripting OK");
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

    script_free(G);

    // Game-owned archetypes (registered separately from the system archetypes).
    if (G->archetypes) {
      for (int i = 0; i < MAX_ARCHETYPES; i++)
        ECS_EntityFreeArchetype(G->archetypes[i]);
      free(G->archetypes);
      G->archetypes = NULL;
    }

    // Image textures must be destroyed before the renderer that owns them.
    if (G->images) {
      for (int i = 0; i < IMAGE_COUNT; i++)
        if (G->images[i]) SDL_DestroyTexture(G->images[i]);
      free(G->images);
      G->images = NULL;
    }

    // Clay/TTF renderer resources. (font count must match load_fonts())
    if (G->clayRendererData) {
      // Free cached TTF_Text + per-size fonts while the text engine and base
      // fonts are still alive.
      SDL_Clay_RenderShutdown();
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
        // Back out one level: a demo (level) returns to the demo picker it was
        // launched from, not all the way to the main menu; menus go to the main
        // menu.
        G->state->sceneId = (G->state->sceneId == SCENE_LEVEL)
                                ? SCENE_MAIN_MENU_DEMOS
                                : SCENE_MAIN_MENU;
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
      // Forward gameplay keys to the script layer (level only).
      dispatch_key_to_script(G, G->event.key.scancode);
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
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
        ui_lua_note_click();   // let Lua ui.button() detect this press
      };

      break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
      Clay_SetPointerState((Clay_Vector2) { G->mouse->x, G->mouse->y }, false);
      break;

    case SDL_EVENT_MOUSE_MOTION: {
      // Always update on motion. The previous gate compared prev-vs-current
      // instead of event-vs-current, so a single duplicate-position event left
      // prevx==x && prevy==y and froze all further updates (Clay stopped
      // receiving the pointer position, killing hover and clicks) until a
      // window re-enter reset prev. Just forward every motion event.
      float lx, ly;
      window_to_logical(G, G->event.motion.x, G->event.motion.y, &lx, &ly);
      G->mouse->prevx = G->mouse->x;
      G->mouse->prevy = G->mouse->y;
      G->mouse->x = (int)lx;
      G->mouse->y = (int)ly;
      Clay_SetPointerState((Clay_Vector2) { G->mouse->x, G->mouse->y }, false);
      break;
    }

    // Touch: a finger acts like the mouse pointer (drives Clay hover/clicks and
    // ui.button) and is also recorded for the touch-driven demos. Finger coords
    // are normalized to the window, so scale by the window size, then map into
    // logical space. SDL also synthesizes mouse events from touches on some
    // platforms, but handling fingers explicitly keeps multi-touch and the
    // demos' touch_pos() working on mobile/web.
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP: {
      int ww, wh;
      SDL_GetWindowSize(G->window, &ww, &wh);
      float lx, ly;
      window_to_logical(G, G->event.tfinger.x * ww, G->event.tfinger.y * wh, &lx, &ly);
      bool down = (G->event.type != SDL_EVENT_FINGER_UP);

      G->mouse->x = (int)lx;
      G->mouse->y = (int)ly;
      touch_set(G, G->event.tfinger.fingerID, lx, ly, down);
      Clay_SetPointerState((Clay_Vector2) { lx, ly }, down);

      if (G->event.type == SDL_EVENT_FINGER_DOWN) {
        mouse_click_events(G);
        ui_lua_note_click();   // a tap registers as a button press
      }
      break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
      Clay_UpdateScrollContainers(true, (Clay_Vector2) { G->event.wheel.x, G->event.wheel.x },G->dtime);
      break;
    // The drawable changed (fullscreen toggle, resolution change, web canvas
    // resize): rebuild the adaptive logical space so the world keeps filling the
    // screen. PIXEL_SIZE_CHANGED fires even for non-resizable windows.
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
      game_recompute_presentation(G);
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

// Forward a key press to the script's on_key callback. The level uses it for
// gameplay; the menus use it for keyboard navigation (arrows + Enter). Scenes
// with no script bound get nothing. The SDL key name is lowercased so scripts
// can compare against friendly names like "space", "left" or "return".
static void dispatch_key_to_script(struct Game *G, SDL_Scancode sc) {
  switch (G->state->sceneId) {
  case SCENE_LEVEL:
  case SCENE_MAIN_MENU:
  case SCENE_MAIN_MENU_OPTIONS:
  case SCENE_MAIN_MENU_DEMOS:
  case SCENE_MAIN_MENU_VIDEO:
    break;            // has a script + on_key
  default:
    return;
  }
  const char *name = SDL_GetScancodeName(sc);
  if (!name || !name[0]) return;

  char buf[32];
  size_t i = 0;
  for (; name[i] && i < sizeof(buf) - 1; i++)
    buf[i] = (char)tolower((unsigned char)name[i]);
  buf[i] = '\0';
  script_on_key(G, buf);
}

// The canonical logical render size (see game.h). Defaults to the design size
// until the first presentation is computed.
int g_logical_w = WINDOW_WIDTH;
int g_logical_h = WINDOW_HEIGHT;

void game_recompute_presentation(struct Game *G) {
  // Drive the logical space from the actual drawable (pixel) size so HiDPI is
  // handled too. Fixed reference height, aspect-matched width -> a LETTERBOX
  // presentation whose logical aspect equals the output aspect produces no bars
  // and fills the screen edge to edge.
  int pw = 0, ph = 0;
  SDL_GetWindowSizeInPixels(G->window, &pw, &ph);
  if (pw <= 0 || ph <= 0) { pw = WINDOW_WIDTH; ph = WINDOW_HEIGHT; }

  g_logical_h = WINDOW_HEIGHT;
  g_logical_w = (int)((double)WINDOW_HEIGHT * pw / ph + 0.5);
  if (g_logical_w < 1) g_logical_w = WINDOW_WIDTH;

  SDL_SetRenderLogicalPresentation(G->renderer, g_logical_w, g_logical_h,
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);

  if (G->ui)
    G->ui->clayDimensions =
        (Clay_Dimensions){ (float)g_logical_w, (float)g_logical_h };

  script_update_screen_dims(G);
}

void game_update(struct Game *G) {
  // Each scene is driven by a Lua script (menus and gameplay alike). Load the
  // one this scene wants, then run its per-frame logic.
  const char *want = NULL;
  switch (G->state->sceneId) {
    case SCENE_MAIN_MENU:
    case SCENE_MAIN_MENU_OPTIONS:
    case SCENE_MAIN_MENU_DEMOS:
    case SCENE_MAIN_MENU_VIDEO:
      want = "scripts/menu.lua";
      break;
    case SCENE_LEVEL:
      want = (G->state->mode == MODE_BENCHMARK) ? "scripts/benchmark.lua"
           : (G->state->mode == MODE_SLOTS)     ? "scripts/slots.lua"
           : (G->state->mode == MODE_CUBE)      ? "scripts/cube.lua"
                                                : "scripts/invaders.lua";
      break;
    default:
      break;
  }
  if (want) {
    const char *cur = script_current_path(G);
    if (!cur || strcmp(cur, want) != 0) script_load(G, want);
    script_update(G, G->dtime);
  } else if (script_current_path(G)) {
    script_unload(G);
  }

  // Per-scene logic that must run before layout could go here. ECS updates run
  // in the render phase (see game_render) so the sprite render system draws
  // after SDL_RenderClear instead of being immediately erased.
  ui_update(G);
}

void game_render(struct Game *G) {
  // Clear to black explicitly. SDL_RenderClear uses the current draw colour,
  // which is left as whatever the Clay renderer last set (e.g. white from the
  // menu tile borders); without this the level/benchmark would clear to that
  // leaked colour instead of black.
  SDL_SetRenderDrawColor(G->renderer, 0, 0, 0, 255);
  SDL_RenderClear(G->renderer);

  // ECS render systems (e.g. SpriteRenderSystem) draw here, after the clear and
  // before the UI overlay. VelocitySystem logic also runs in this pass.
  // The sprite system queues quads into the batch; we flush them in one draw.
  if (G->state->sceneId == SCENE_LEVEL) {
    ECS_Update(G->ecs);
    // Collision runs after movement so it sees this frame's positions; it may
    // call into Lua (on_collision), so it must stay on the main thread.
    collision_update(G);
  }

  // Software 3D primitives (g3d.*) emitted from on_ui this frame: draw over the
  // sprites and under the Clay UI overlay.
  g3d_flush(G->renderer);

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

  // Hot-reload the game scripts when the file changes (designer iteration).
  script_check_reload(G);

  // The level runs uncapped (the benchmark needs to measure true frame rate);
  // the menus are capped to avoid spinning the CPU.
  if (G->state->sceneId != SCENE_LEVEL) {
#ifndef __EMSCRIPTEN__
    // On the web the browser paces frames (requestAnimationFrame), no delay.
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
  web_fit_canvas(G);
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
