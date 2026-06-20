#include "load_f.h"

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
  fonts[0] =
      TTF_OpenFont("assets/fonts/LiberationSans/LiberationSans-Regular.ttf", 24);
  if (!fonts[0]) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s",
                 SDL_GetError());
    // return SDL_APP_FAILURE;
  }

  fonts[1] =
      TTF_OpenFont("assets/fonts/LiberationSans/LiberationSans-Bold.ttf", 24);
  if (!fonts[1]) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s",
                 SDL_GetError());
    // return SDL_APP_FAILURE;
  }

  return fonts;
}
