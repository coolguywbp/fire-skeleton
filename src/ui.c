#include "ui.h"
#include "game.h"
#include "ecs_entity.h"
#include "script.h"
#include "main.h"
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_touch.h>
#include <math.h>
#include <stdio.h>

void ui_main_menu(struct Game *G, Clay_Sizing *claySize);
void ui_options_menu(struct Game *G, Clay_Sizing *claySize);
void ui_level(struct Game *G, Clay_Sizing *claySize);

void ui_fps(float frameRate);
void ui_object_count(struct Game *G);
void ui_hud(struct Game *G);


Clay_String ToClayString(char* string);

void ui_click_events(struct Game *G){
  if (Clay_PointerOver(Clay_GetElementId(ToClayString("PlayButton")))){
    G->state->mode = MODE_INVADERS;
    G->state->sceneId = SCENE_LEVEL;
  }
  if (Clay_PointerOver(Clay_GetElementId(ToClayString("BenchmarkButton")))){
    G->state->mode = MODE_BENCHMARK;
    G->state->sceneId = SCENE_LEVEL;
  }
  if (Clay_PointerOver(Clay_GetElementId(ToClayString("OptionsButton")))){
    G->state->sceneId = SCENE_MAIN_MENU_OPTIONS;
  }
  if (Clay_PointerOver(Clay_GetElementId(ToClayString("BackToMainMenuButton")))){
    G->state->sceneId = SCENE_MAIN_MENU;
  }
  if (Clay_PointerOver(Clay_GetElementId(ToClayString("ExitButton")))){
    G->is_running = false;
  }
}

bool ui_create_layout(struct Game *G) {
  Clay_Sizing layoutExpand = {.width = CLAY_SIZING_GROW(0),
                              .height = CLAY_SIZING_GROW(0)};

  switch (G->state->sceneId){
  case SCENE_MAIN_MENU:
    ui_main_menu(G, &layoutExpand);
    break;
  case SCENE_MAIN_MENU_OPTIONS:
    ui_options_menu(G, &layoutExpand);
    break;
  case SCENE_LEVEL:
    ui_level(G, &layoutExpand);
    break;
  default:
    ui_main_menu(G, &layoutExpand);
    break;
  }

  #ifdef SHOW_FPS
  ui_fps(G->frameRate);
  #endif /* ifdef SHOW_FPS */

  // Object counter and benchmark status: only shown in the level, not the menus.
  if (G->state->sceneId == SCENE_LEVEL) {
    ui_object_count(G);
    ui_hud(G);
    script_on_ui(G);   // script-drawn UI (ui.* immediate-mode toolkit)
  }
  
  return true;
};




void ui_level(struct Game *G, Clay_Sizing *claySize){

}


void ui_options_menu(struct Game *G, Clay_Sizing *claySize){
  MenuItem *menuItems[] = {
    &(MenuItem){.caption = "AUDIO", .id = "AudioButton"},
    &(MenuItem){.caption = "VIDEO", .id = "VideoButton"},
    &(MenuItem){.caption = "BACK", .id = "BackToMainMenuButton"}
  };
  CLAY(CLAY_ID("OuterContainer"),{
       .layout = {
        .layoutDirection = CLAY_TOP_TO_BOTTOM,
        .sizing = *claySize,
        .padding = {.bottom=200, .top=200, .left=100, .right=100},
        .childGap = 96
       },
       .backgroundColor = {0,0,0,255}
  }){
      CLAY(CLAY_ID("MenuItems"), {
          .layout = { 
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(300),
            .height = CLAY_SIZING_GROW(0) },
            .padding = CLAY_PADDING_ALL(16),
            .childGap = 16 },
      }) {
          for (int i = 0; i < 3; i++) {
            
            MenuItem *item = menuItems[i];

            CLAY(CLAY_SID(ToClayString(item->id)), {
              .layout = {
                .sizing = CLAY_SIZING_GROW(1)
              }
            }){

              CLAY_TEXT(ToClayString(item->caption),CLAY_TEXT_CONFIG({
                .fontSize = 54,
                .textColor = {255, 255, 255, 255},
                .fontId = Clay_Hovered() ? 1 : 0
                })
              );
              };
          }
      }

    }


}
void ui_main_menu(struct Game *G, Clay_Sizing *claySize){

  MenuItem *menuItems[] = {
    &(MenuItem){.caption = "PLAY", .id = "PlayButton"},
    &(MenuItem){.caption = "BENCHMARK", .id = "BenchmarkButton"},
    &(MenuItem){.caption = "OPTIONS", .id = "OptionsButton"},
    &(MenuItem){.caption = "EXIT", .id = "ExitButton"}
  };
  const int menuItemCount = 4;
  CLAY(CLAY_ID("OuterContainer"),{
       .layout = {
        .layoutDirection = CLAY_TOP_TO_BOTTOM,
        .sizing = *claySize,
        .padding = {.bottom=200, .top=200, .left=100, .right=100},
        .childGap = 164
       },
       .backgroundColor = {0,0,0,255}
  }){
    CLAY(CLAY_ID("TitleContainer"), {
      .layout = {
         .sizing = CLAY_SIZING_GROW(0)
      }})
      {
      CLAY_TEXT(
        CLAY_STRING("FIRE SKELETON INVADER"),CLAY_TEXT_CONFIG({
        .fontSize = 130,
        .lineHeight = 120,
        .textColor = {255, 255, 255, 255},
        .fontId = 1
        }));
      };
    
    CLAY(CLAY_ID("MenuItems"), {
          .layout = { 
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = { .width = CLAY_SIZING_FIXED(300),
            .height = CLAY_SIZING_GROW(0) },
            .padding = CLAY_PADDING_ALL(16),
            .childGap = 16 },
      }) {
          for (int i = 0; i < menuItemCount; i++) {

            MenuItem *item = menuItems[i];

            CLAY(CLAY_SID(ToClayString(item->id)), {
              .layout = {
                .sizing = CLAY_SIZING_GROW(1)
              }
            }){

              CLAY_TEXT(ToClayString(item->caption),CLAY_TEXT_CONFIG({
                .fontSize = 54,
                .textColor = {255, 255, 255, 255},
                .fontId = Clay_Hovered() ? 1 : 0
                })
              );
              };
          }
      }
    //FIRE SKULL ANIMATION
    float offset, amplitude, speed;
    amplitude = 50;
    speed = 500.;
    offset = sin(SDL_GetTicks() / speed) * amplitude;

    CLAY(CLAY_ID("MenuFireSkeleton"), {    
    .floating = {.offset={.x=-100, .y=150 + offset}, .attachTo=CLAY_ATTACH_TO_PARENT, .zIndex = 99, .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP }},
    .layout = {.sizing = {.width = CLAY_SIZING_FIXED(450)}},
    .aspectRatio = {450./644.},
    .image = G->images[0] }){};

  };
}

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
