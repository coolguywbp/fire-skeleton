#ifndef CLAY_RENDERER_H
#define CLAY_RENDERER_H

#include "game.h"

void SDL_Clay_RenderClayCommands(Clay_SDL3RendererData *rendererData,
                                 Clay_RenderCommandArray *rcommands);

// A TTF_Font handle for (fontId, size), opened once and reused so a shared font
// is never resized per frame (which thrashes the glyph atlas). Used by both the
// renderer and the Clay text-measure callback.
TTF_Font *SDL_Clay_GetSizedFont(Clay_SDL3RendererData *rendererData, int fontId,
                                float size);

// Free the renderer's cached TTF_Text objects and per-size fonts. Call before
// destroying the text engine and base fonts (i.e. on shutdown).
void SDL_Clay_RenderShutdown(void);

#endif // CLAY_RENDERER_H
