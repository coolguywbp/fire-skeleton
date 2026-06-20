#include "init_clay.h"
#include "ui.h"
#include "ui_lua.h"
#include "clay_renderer.h"

void HandleClayErrors(Clay_ErrorData errorData) {
  // See the Clay_ErrorData struct for more information
  printf("%s", errorData.errorText.chars);
  // switch (errorData.errorType) {
  //   // etc
  // }
}

Clay_Dimensions SDL_MeasureText(Clay_StringSlice text,
                                Clay_TextElementConfig *config,
                                void *userData) {
  Clay_SDL3RendererData *rendererData = userData;
  // Use the same per-size font handle as the renderer, so measuring never
  // resizes a shared font (which would flush the glyph atlas every layout).
  TTF_Font *font =
      SDL_Clay_GetSizedFont(rendererData, config->fontId, config->fontSize);
  int width, height;

  if (!TTF_GetStringSize(font, text.chars, text.length, &width, &height)) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to measure text: %s",
                 SDL_GetError());
  }

  return (Clay_Dimensions){(float)width, (float)height};
}

bool ui_init_clay(struct Game *G) {
  if (!G->renderer) {
    fprintf(stderr,
            "Error getting SDL renderer while initializing Clay UI: %s\n",
            SDL_GetError());
    return false;
  }

  struct UI *ui = malloc(sizeof(struct UI));
  if (!ui) {
    fprintf(stderr, "Error: Failed to allocate memory for UI struct\n");
    return false;
  }
  uint64_t totalMemorySize = Clay_MinMemorySize();
  Clay_Arena clayMemory = (Clay_Arena){.memory = SDL_malloc(totalMemorySize),
                                       .capacity = totalMemorySize};
  // Match the adaptive logical space computed in game_init_sdl (fixed height,
  // aspect-matched width) so the UI lays out across the whole screen.
  Clay_Dimensions dimensions =
      (Clay_Dimensions){ (float)g_logical_w, (float)g_logical_h };
  ui->clayDimensions = dimensions;

  if (!Clay_Initialize(clayMemory, dimensions,
                       (Clay_ErrorHandler){HandleClayErrors})) {
    fprintf(stderr, "Error initializing Clay UI: %s\n", SDL_GetError());
    return false;
  };
  Clay_SetMeasureTextFunction(SDL_MeasureText, G->clayRendererData);

  G->ui = ui;

  return true;
};

bool ui_update(struct Game *G) {
  Clay_SetLayoutDimensions(G->ui->clayDimensions);
  ui_lua_begin_frame();          // reset the per-frame UI string/id arena
  Clay_BeginLayout();
  ui_create_layout(G);
  G->ui->renderCommands = Clay_EndLayout();
  ui_lua_end_frame();            // clear any unconsumed click
  return true;
};


