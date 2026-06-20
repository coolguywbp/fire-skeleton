#include "ui.h"
#include "game.h"
#include "ecs_entity.h"
#include "script.h"
#include "main.h"
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_touch.h>
#include <math.h>
#include <stdio.h>

void ui_fps(float frameRate);
void ui_object_count(struct Game *G);
void ui_hud(struct Game *G);

Clay_String ToClayString(char* string);

// Menu/HUD clicks are handled in Lua now (ui.button); the C side only forwards
// the press via ui_lua_note_click() in game_events, so this is a no-op.
void ui_click_events(struct Game *G){
  (void)G;
}

bool ui_create_layout(struct Game *G) {
  // Every scene draws its UI from its Lua script (menus and gameplay alike)
  // via on_ui() and the `ui` toolkit.
  script_on_ui(G);

  #ifdef SHOW_FPS
  ui_fps(G->frameRate);
  #endif /* ifdef SHOW_FPS */

  // Live object count + scripted status line: only in the level.
  if (G->state->sceneId == SCENE_LEVEL) {
    ui_object_count(G);
    ui_hud(G);
  }

  return true;
};

void ui_fps(float frameRate) {
  // Must be static: Clay stores the pointer and only measures the text later,
  // inside Clay_EndLayout(), after this function has returned. A stack buffer
  // would dangle (stack-use-after-return). Size 4 fits "999\0".
  static char fpsStr[4];
  int fps = frameRate > 999 ? 999 : (int)roundf(frameRate);
  snprintf(fpsStr, sizeof(fpsStr), "%d", fps);

  CLAY(CLAY_ID("FPS"), {
      .floating = {
        .offset = {.x = -10, .y = 5},
        .attachTo = CLAY_ATTACH_TO_ROOT,
        .zIndex = 999,
        .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP }
      }
    }) {
    CLAY_TEXT(ToClayString(fpsStr), CLAY_TEXT_CONFIG({
      .fontSize = 24,
      .textColor = {255, 255, 255, 255},
      .fontId = 0
    }));
  }
}

void ui_object_count(struct Game *G) {
  // Static for the same lifetime reason as ui_fps's buffer.
  static char objStr[24];
  snprintf(objStr, sizeof(objStr), "Objects: %zu", ECS_EntityCount(G->ecs));

  // Separate label, placed to the left of the FPS counter (top-right).
  CLAY(CLAY_ID("ObjectCount"), {
      .floating = {
        .offset = {.x = -70, .y = 5},
        .attachTo = CLAY_ATTACH_TO_ROOT,
        .zIndex = 999,
        .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP }
      }
    }) {
    CLAY_TEXT(ToClayString(objStr), CLAY_TEXT_CONFIG({
      .fontSize = 24,
      .textColor = {255, 255, 255, 255},
      .fontId = 0
    }));
  }
 }

void ui_hud(struct Game *G) {
  // A script-provided status line (set via the Lua hud() function). Nothing to
  // draw if no script set one this frame.
  if (G->hud_text[0] == '\0') return;

  CLAY(CLAY_ID("Hud"), {
      .floating = {
        .offset = {.x = 10, .y = 5},
        .attachTo = CLAY_ATTACH_TO_ROOT,
        .zIndex = 999,
        .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP }
      }
    }) {
    CLAY_TEXT(ToClayString(G->hud_text), CLAY_TEXT_CONFIG({
      .fontSize = 24,
      .textColor = {137, 180, 250, 255},
      .fontId = 0
    }));
  }
}


Clay_String ToClayString (char* string){
  return (Clay_String){false, strlen(string), string};
};
