#include "load_f.h"

TTF_Font **load_fonts(struct Game *G) {
  TTF_Font **fonts = SDL_calloc(2, sizeof(TTF_Font *));
  if (!fonts) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to allocate memory for the font array: %s",
                 SDL_GetError());
    // return SDL_APP_FAILURE;
  }

  fonts[0] =
      TTF_OpenFont("assets/fonts/NeueMontreal/NeueMontreal-Regular.ttf", 24);
  if (!fonts[0]) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s",
                 SDL_GetError());
    // return SDL_APP_FAILURE;
  }

  fonts[1] =
      TTF_OpenFont("assets/fonts/NeueMontreal/NeueMontreal-Bold.ttf", 24);
  if (!fonts[1]) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s",
                 SDL_GetError());
    // return SDL_APP_FAILURE;
  }

  return fonts;
}
