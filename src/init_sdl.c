#include "init_sdl.h"
#include "load_f.h"
#include "logger.h"
#include <stdlib.h>

bool game_init_sdl(struct Game *G) {
  
  if (!SDL_Init(SDL_FLAGS)) {
    fprintf(stderr, "Error initializing SDL3: %s\n", SDL_GetError());
    return false;
  }

  if (!TTF_Init()) {
    fprintf(stderr, "Error initializing SDL3_ttf: %s\n", SDL_GetError());
    return false;
  }
  
  G->window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MOUSE_CAPTURE);
  if (!G->window) {
    fprintf(stderr, "Error creating Window: %s\n", SDL_GetError());
    return false;
  }

 
  G->renderer = SDL_CreateRenderer(G->window, NULL);
  if (!G->renderer) {
    fprintf(stderr, "Error creating Renderer: %s\n", SDL_GetError());
    return false;
  }

  
  SDL_DisplayID primary_display = SDL_GetPrimaryDisplay();
  LOG_INFO("Primary display ID: %u", primary_display);
  
  float render_scale_x, render_scale_y,monitor_scale, display_scale, pixel_density;
  monitor_scale = SDL_GetDisplayContentScale(primary_display);
  display_scale = SDL_GetWindowDisplayScale(G->window);
  pixel_density = SDL_GetWindowPixelDensity(G->window);
  SDL_GetRenderScale(G->renderer, &render_scale_x, &render_scale_y);
  
  LOG_INFO("Monitor scale: %.2f | Display scale: %.2f | Pixel density: %.2f", monitor_scale, display_scale, pixel_density);
  LOG_INFO("Render scale: %.2fx, %.2fy", render_scale_x, render_scale_y);
  
  int window_width, window_height;
  int render_width, render_height;
  int pixel_width, pixel_height;
  
  SDL_GetWindowSize(G->window, &window_width, &window_height);
  SDL_GetRenderOutputSize(G->renderer, &render_width, &render_height);
  SDL_GetWindowSizeInPixels(G->window, &pixel_width, &pixel_height);

  LOG_INFO("Window logical size: %dx%d", window_width, window_height);
  LOG_INFO("Window size in pixels: %dx%d", pixel_width, pixel_height);
  LOG_INFO("Renderer output size: %dx%d", render_width, render_height);

  G->textEngine = TTF_CreateRendererTextEngine(G->renderer);
  if (!G->textEngine) {
    fprintf(stderr, "Error creating TextEngine: %s", SDL_GetError());
    return false;
  }
  G->clayRendererData = malloc(sizeof(Clay_SDL3RendererData));
  G->clayRendererData->renderer = G->renderer;
  G->clayRendererData->textEngine = G->textEngine;
  G->clayRendererData->fonts = load_fonts(G);
  if (!G->clayRendererData) {
    fprintf(stderr, "Error creating ClayRendererData: %s", SDL_GetError());
    return false;
  }

  // SDL_Surface *icon_surf = IMG_Load("images/C-logo.png");
  // if (!icon_surf) {
  //   fprintf(stderr, "Error loading Surface: %s\n", SDL_GetError());
  //   return false;
  // }

  // if (!SDL_SetWindowIcon(g->window, icon_surf)) {
  //   fprintf(stderr, "Error setting Window Icon: %s\n", SDL_GetError());
  //   SDL_DestroySurface(icon_surf);
  //   return false;
  // }
  // SDL_DestroySurface(icon_surf);

  return true;
}
