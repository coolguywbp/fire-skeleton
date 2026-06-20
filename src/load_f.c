#include "load_f.h"

// Font file paths, indexed by fontId (0 = regular, 1 = bold). Single source of
// truth shared by load_fonts() and the renderer's per-size font cache.
static const char *const kFontPaths[2] = {
  "assets/fonts/LiberationSans/LiberationSans-Regular.ttf",
  "assets/fonts/LiberationSans/LiberationSans-Bold.ttf",
};

const char **load_font_paths(void) {
  return (const char **)kFontPaths;
}

TTF_Font **load_fonts(struct Game *G) {
  TTF_Font **fonts = SDL_calloc(2, sizeof(TTF_Font *));
  if (!fonts) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to allocate memory for the font array: %s",
                 SDL_GetError());
    // return SDL_APP_FAILURE;
  }

  // Liberation Sans: a free (SIL OFL) metric-compatible clone of Helvetica/Arial
  // with full Cyrillic coverage. See assets/fonts/LiberationSans/LICENSE.
  fonts[0] = TTF_OpenFont(kFontPaths[0], 24);
  if (!fonts[0]) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s",
                 SDL_GetError());
    // return SDL_APP_FAILURE;
  }

  fonts[1] = TTF_OpenFont(kFontPaths[1], 24);
  if (!fonts[1]) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s",
                 SDL_GetError());
    // return SDL_APP_FAILURE;
  }

  return fonts;
}
