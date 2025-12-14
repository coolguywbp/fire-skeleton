#pragma once
#ifndef RENDERER_H
#define RENDERER_H

#include "game.h"

typedef struct color {
    float r, g, b, a;
} color;

typedef enum RENDER_COMMAND_TYPE {
    RENDER_COMMAND_TYPE_NONE,
    RENDER_COMMAND_TYPE_RECTANGLE,
    RENDER_COMMAND_TYPE_TEXT,
    RENDER_COMMAND_TYPE_IMAGE,
} RenderCommandType;

typedef struct BoundingBox {
    float x, y, w, h;
} BoundingBox;

typedef struct RectangleRenderData {
    color backgroundColor;
} RectangleRenderData;

typedef struct ImageRenderData {
    void* imageData;
} ImageRenderData;

typedef union RenderData {
    RectangleRenderData rectangle;
    // TextRenderData text;
    ImageRenderData image;
} RenderData;

typedef struct RenderCommand{
    BoundingBox boundingBox;
    RenderData renderData;
    void *userData;
    uint32_t id;
    int16_t zIndex;
    RenderCommandType commandType;
} RenderCommand;

typedef struct RenderCommandArray{
  int32_t capacity;
  int32_t length;
  RenderCommand* internalArray;
}RenderCommandArray;

RenderCommandArray SDL_RenderCommands(SDL_Renderer * renderer, RenderCommandArray *rcommands);

#endif
