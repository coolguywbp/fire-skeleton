#ifndef ECS_RENDERER_H
#define ECS_RENDERER_H

#include "game.h"

typedef struct ECS_RenderCommandArray{
  // The underlying max capacity of the array, not necessarily all initialized.
  int32_t capacity;
  // The number of initialized elements in this array. Used for loops and iteration.
  int32_t length;
  // A pointer to the first element in the internal array.
  ECS_RenderCommand* internalArray;

}ECS_RenderCommandArray;

SDL_ECS_RenderCommands(SDL_Renderer * renderer, ECS_RenderCommandArray *ecsRenderCommands);

#endif // ECS_RENDERER_H
