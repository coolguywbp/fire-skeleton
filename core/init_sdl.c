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
  
  // On the web the canvas is a fixed-size element: high-DPI scaling makes the
  // drawing buffer devicePixelRatio-times larger than the canvas, which both
  // distorts the image (non-uniform CSS fit) and offsets mouse coordinates
  // (SDL maps them to the scaled buffer). Mouse capture is also meaningless in
  // a browser. Keep both only on the native build.
  SDL_WindowFlags wflags = SDL_WINDOW_OPENGL;
#ifdef __EMSCRIPTEN__
  // The browser canvas is resized to the viewport (see web_fit_canvas);
  // RESIZABLE lets SDL pick up those size changes. HIGH_PIXEL_DENSITY makes the
  // drawing buffer match the display's device pixels (devicePixelRatio) instead
  // of CSS pixels, so on HiDPI/retina screens the world is rendered at full
  // resolution rather than upscaled by the browser (which looked blurry). The
  // earlier worry about offset input no longer applies: pointer/finger coords
  // are mapped through SDL_RenderCoordinatesFromWindow (see game_events).
  wflags |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
  // Native: high-DPI + mouse capture. Intentionally NOT resizable -- a window
  // with fixed size hints keeps tiling WMs (e.g. Hyprland) floating it at its
  // native 1280x960 (so it stays crisp, 1:1, not stretched into a tile). The
  // VIDEO menu still changes resolution/fullscreen programmatically, which
  // doesn't require the user-resizable flag.
  wflags |= SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MOUSE_CAPTURE;
#endif
  G->window = SDL_CreateWindow(WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, wflags);
  if (!G->window) {
    fprintf(stderr, "Error creating Window: %s\n", SDL_GetError());
    return false;
  }

 
  // On Windows prefer the OpenGL renderer. SDL would otherwise pick Direct3D,
  // and the D3D9 path (what wine exposes) renders textures black; OpenGL works
  // both under wine and on native Windows.
#ifdef _WIN32
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif
  G->renderer = SDL_CreateRenderer(G->window, NULL);
  if (!G->renderer) {
    fprintf(stderr, "Error creating Renderer: %s\n", SDL_GetError());
    return false;
  }

  // Disable vsync so the benchmark can measure frame rates above the monitor's
  // refresh rate.
  SDL_SetRenderVSync(G->renderer, 0);

  // Resolution independence: draw into a logical space (fixed height, width
  // matched to the window aspect) and let SDL scale it to the actual
  // window/canvas/fullscreen size. Matching the aspect means it fills the screen
  // with no letterbox bars. Every script coordinate (and Clay's layout) is in
  // this logical space; SDL_RenderCoordinatesFromWindow maps input back into it
  // (see game_events), so mouse and touch line up at any size. Recomputed on
  // every window-size change (game_recompute_presentation).
  game_recompute_presentation(G);

  
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
  G->clayRendererData->font_paths = load_font_paths();
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
