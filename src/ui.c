#include "ui.h"
#include "game.h"
#include "main.h"
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_touch.h>
#include <math.h>
#include <stdio.h>

void ui_main_menu(struct Game *G, Clay_Sizing *claySize);
void ui_options_menu(struct Game *G, Clay_Sizing *claySize);
void ui_level(struct Game *G, Clay_Sizing *claySize);

void ui_fps(float frameRate);


Clay_String ToClayString(char* string);

void ui_click_events(struct Game *G){
  if (Clay_PointerOver(Clay_GetElementId(ToClayString("StartButton")))){
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
    &(MenuItem){.caption = "START", .id = "StartButton"},
    &(MenuItem){.caption = "OPTIONS", .id = "OptionsButton"},
    &(MenuItem){.caption = "EXIT", .id = "ExitButton"}
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
    CLAY(CLAY_ID("TitleContainer"), {
      .layout = {
         .sizing = CLAY_SIZING_GROW(0)
      }})
      {
      CLAY_TEXT(
        CLAY_STRING("FIRE SKELETON INVADER"),CLAY_TEXT_CONFIG({
        .fontSize = 138,
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
  char fpsStr[3];
  int fps;

  if (frameRate > 999){
    fps = 999;
  }else{
    fps = roundf(frameRate);
  }

  snprintf(fpsStr, sizeof(fpsStr), "%d", fps);
  
  CLAY(CLAY_ID("FPS"),{
      .floating = {
        .offset={.x=-10, .y=5},
        .attachTo=CLAY_ATTACH_TO_ROOT,
        .zIndex = 999,
        .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP }
      }
    }){
    CLAY_TEXT(ToClayString(fpsStr), CLAY_TEXT_CONFIG({
      .fontSize = 24,
      .textColor = {255, 255, 255, 255},
      .fontId = 0
    })
  );
  }
 }


Clay_String ToClayString (char* string){
  return (Clay_String){false, strlen(string), string};
};
