#include "clay_renderer.h"

#include <stdint.h>
#include <string.h>

/* Global for convenience. Even in 4K this is enough for smooth curves (low
 * radius or rect size coupled with no AA or low resolution might make it appear
 * as jagged curves) */
static int NUM_CIRCLE_SEGMENTS = 16;

// ---------------------------------------------------------------------------
// Text caching
//
// The naive text path re-rasterizes every string every frame (TTF_CreateText +
// TTF_DestroyText) and rescales a shared font with TTF_SetFontSize, which
// flushes the glyph atlas. Native CPUs absorb this; single-threaded WebAssembly
// does not, and it dominates the frame. Two caches remove the cost:
//
//  * sized fonts: one TTF_Font per (fontId, size), opened once from the font
//    file, so a font is never resized after warm-up (the atlas stays warm).
//    Shared by the renderer and the Clay measure callback.
//  * rendered text: TTF_Text objects keyed by (fontId, size, string), created
//    once and redrawn each frame. A per-frame mark-and-sweep frees entries that
//    stop appearing (e.g. a stale FPS value).
// ---------------------------------------------------------------------------

typedef struct {
  int       fontId;
  float     size;
  TTF_Font *font;
  bool      owned;   // opened here (close on shutdown) vs aliased base font
} SizedFont;

static SizedFont g_sized_fonts[64];
static int       g_sized_font_count;

TTF_Font *SDL_Clay_GetSizedFont(Clay_SDL3RendererData *r, int fontId,
                                float size) {
  for (int i = 0; i < g_sized_font_count; i++)
    if (g_sized_fonts[i].fontId == fontId && g_sized_fonts[i].size == size)
      return g_sized_fonts[i].font;

  // Miss: open a dedicated handle at this size from the same file.
  TTF_Font *font = NULL;
  bool owned = false;
  if (r->font_paths && r->font_paths[fontId]) {
    font = TTF_OpenFont(r->font_paths[fontId], size);
    owned = (font != NULL);
  }
  if (!font) {                       // last resort: resize the base handle
    font = r->fonts[fontId];
    TTF_SetFontSize(font, size);
  }
  if (g_sized_font_count < 64)
    g_sized_fonts[g_sized_font_count++] =
        (SizedFont){fontId, size, font, owned};
  return font;
}

typedef struct {
  uint64_t  key;
  TTF_Text *text;
  uint32_t  touched;   // frame index this entry was last drawn
  bool      used;
} TextEntry;

#define TEXT_CACHE_CAP 256
static TextEntry g_text_cache[TEXT_CACHE_CAP];
static uint32_t  g_frame;

static uint64_t text_key(int fontId, float size, const char *s, size_t n) {
  uint64_t h = 1469598103934665603ULL;            // FNV-1a, 64-bit
  h = (h ^ (uint8_t)fontId) * 1099511628211ULL;
  uint32_t sb;
  memcpy(&sb, &size, sizeof sb);
  for (int i = 0; i < 4; i++)
    h = (h ^ ((sb >> (i * 8)) & 0xff)) * 1099511628211ULL;
  for (size_t i = 0; i < n; i++)
    h = (h ^ (uint8_t)s[i]) * 1099511628211ULL;
  return h;
}

void SDL_Clay_RenderShutdown(void) {
  for (int k = 0; k < TEXT_CACHE_CAP; k++)
    if (g_text_cache[k].used && g_text_cache[k].text) {
      TTF_DestroyText(g_text_cache[k].text);
      g_text_cache[k].text = NULL;
      g_text_cache[k].used = false;
    }
  for (int i = 0; i < g_sized_font_count; i++)
    if (g_sized_fonts[i].owned && g_sized_fonts[i].font)
      TTF_CloseFont(g_sized_fonts[i].font);
  g_sized_font_count = 0;
}

// all rendering is performed by a single SDL call, avoiding multiple RenderRect
// + plumbing choice for circles.
static void SDL_Clay_RenderFillRoundedRect(Clay_SDL3RendererData *rendererData,
                                           const SDL_FRect rect,
                                           const float cornerRadius,
                                           const Clay_Color _color) {
  const SDL_FColor color = {_color.r / 255, _color.g / 255, _color.b / 255,
                            _color.a / 255};

  int indexCount = 0, vertexCount = 0;

  const float minRadius = SDL_min(rect.w, rect.h) / 2.0f;
  const float clampedRadius = SDL_min(cornerRadius, minRadius);

  const int numCircleSegments =
      SDL_max(NUM_CIRCLE_SEGMENTS, (int)clampedRadius * 0.5f);

  int totalVertices = 4 + (4 * (numCircleSegments * 2)) + 2 * 4;
  int totalIndices = 6 + (4 * (numCircleSegments * 3)) + 6 * 4;

  SDL_Vertex vertices[totalVertices];
  int indices[totalIndices];

  // define center rectangle
  vertices[vertexCount++] =
      (SDL_Vertex){{rect.x + clampedRadius, rect.y + clampedRadius},
                   color,
                   {0, 0}}; // 0 center TL
  vertices[vertexCount++] =
      (SDL_Vertex){{rect.x + rect.w - clampedRadius, rect.y + clampedRadius},
                   color,
                   {1, 0}}; // 1 center TR
  vertices[vertexCount++] = (SDL_Vertex){
      {rect.x + rect.w - clampedRadius, rect.y + rect.h - clampedRadius},
      color,
      {1, 1}}; // 2 center BR
  vertices[vertexCount++] =
      (SDL_Vertex){{rect.x + clampedRadius, rect.y + rect.h - clampedRadius},
                   color,
                   {0, 1}}; // 3 center BL

  indices[indexCount++] = 0;
  indices[indexCount++] = 1;
  indices[indexCount++] = 3;
  indices[indexCount++] = 1;
  indices[indexCount++] = 2;
  indices[indexCount++] = 3;

  // define rounded corners as triangle fans
  const float step = (SDL_PI_F / 2) / numCircleSegments;
  for (int i = 0; i < numCircleSegments; i++) {
    const float angle1 = (float)i * step;
    const float angle2 = ((float)i + 1.0f) * step;

    for (int j = 0; j < 4; j++) { // Iterate over four corners
      float cx, cy, signX, signY;

      switch (j) {
      case 0:
        cx = rect.x + clampedRadius;
        cy = rect.y + clampedRadius;
        signX = -1;
        signY = -1;
        break; // Top-left
      case 1:
        cx = rect.x + rect.w - clampedRadius;
        cy = rect.y + clampedRadius;
        signX = 1;
        signY = -1;
        break; // Top-right
      case 2:
        cx = rect.x + rect.w - clampedRadius;
        cy = rect.y + rect.h - clampedRadius;
        signX = 1;
        signY = 1;
        break; // Bottom-right
      case 3:
        cx = rect.x + clampedRadius;
        cy = rect.y + rect.h - clampedRadius;
        signX = -1;
        signY = 1;
        break; // Bottom-left
      default:
        return;
      }

      vertices[vertexCount++] =
          (SDL_Vertex){{cx + SDL_cosf(angle1) * clampedRadius * signX,
                        cy + SDL_sinf(angle1) * clampedRadius * signY},
                       color,
                       {0, 0}};
      vertices[vertexCount++] =
          (SDL_Vertex){{cx + SDL_cosf(angle2) * clampedRadius * signX,
                        cy + SDL_sinf(angle2) * clampedRadius * signY},
                       color,
                       {0, 0}};

      indices[indexCount++] =
          j; // Connect to corresponding central rectangle vertex
      indices[indexCount++] = vertexCount - 2;
      indices[indexCount++] = vertexCount - 1;
    }
  }

  // Define edge rectangles
  //  Top edge
  vertices[vertexCount++] =
      (SDL_Vertex){{rect.x + clampedRadius, rect.y}, color, {0, 0}}; // TL
  vertices[vertexCount++] = (SDL_Vertex){
      {rect.x + rect.w - clampedRadius, rect.y}, color, {1, 0}}; // TR

  indices[indexCount++] = 0;
  indices[indexCount++] = vertexCount - 2; // TL
  indices[indexCount++] = vertexCount - 1; // TR
  indices[indexCount++] = 1;
  indices[indexCount++] = 0;
  indices[indexCount++] = vertexCount - 1; // TR
  // Right edge
  vertices[vertexCount++] = (SDL_Vertex){
      {rect.x + rect.w, rect.y + clampedRadius}, color, {1, 0}}; // RT
  vertices[vertexCount++] = (SDL_Vertex){
      {rect.x + rect.w, rect.y + rect.h - clampedRadius}, color, {1, 1}}; // RB

  indices[indexCount++] = 1;
  indices[indexCount++] = vertexCount - 2; // RT
  indices[indexCount++] = vertexCount - 1; // RB
  indices[indexCount++] = 2;
  indices[indexCount++] = 1;
  indices[indexCount++] = vertexCount - 1; // RB
  // Bottom edge
  vertices[vertexCount++] = (SDL_Vertex){
      {rect.x + rect.w - clampedRadius, rect.y + rect.h}, color, {1, 1}}; // BR
  vertices[vertexCount++] = (SDL_Vertex){
      {rect.x + clampedRadius, rect.y + rect.h}, color, {0, 1}}; // BL

  indices[indexCount++] = 2;
  indices[indexCount++] = vertexCount - 2; // BR
  indices[indexCount++] = vertexCount - 1; // BL
  indices[indexCount++] = 3;
  indices[indexCount++] = 2;
  indices[indexCount++] = vertexCount - 1; // BL
  // Left edge
  vertices[vertexCount++] = (SDL_Vertex){
      {rect.x, rect.y + rect.h - clampedRadius}, color, {0, 1}}; // LB
  vertices[vertexCount++] =
      (SDL_Vertex){{rect.x, rect.y + clampedRadius}, color, {0, 0}}; // LT

  indices[indexCount++] = 3;
  indices[indexCount++] = vertexCount - 2; // LB
  indices[indexCount++] = vertexCount - 1; // LT
  indices[indexCount++] = 0;
  indices[indexCount++] = 3;
  indices[indexCount++] = vertexCount - 1; // LT

  // Render everything
  SDL_RenderGeometry(rendererData->renderer, NULL, vertices, vertexCount,
                     indices, indexCount);
}

static void SDL_Clay_RenderArc(Clay_SDL3RendererData *rendererData,
                               const SDL_FPoint center, const float radius,
                               const float startAngle, const float endAngle,
                               const float thickness, const Clay_Color color) {
  SDL_SetRenderDrawColor(rendererData->renderer, color.r, color.g, color.b,
                         color.a);

  const float radStart = startAngle * (SDL_PI_F / 180.0f);
  const float radEnd = endAngle * (SDL_PI_F / 180.0f);

  const int numCircleSegments =
      SDL_max(NUM_CIRCLE_SEGMENTS,
              (int)(radius * 1.5f)); // increase circle segments for larger
                                     // circles, 1.5 is arbitrary.

  const float angleStep = (radEnd - radStart) / (float)numCircleSegments;
  const float thicknessStep =
      0.4f; // arbitrary value to avoid overlapping lines. Changing
            // THICKNESS_STEP or numCircleSegments might cause artifacts.

  for (float t = thicknessStep; t < thickness - thicknessStep;
       t += thicknessStep) {
    SDL_FPoint points[numCircleSegments + 1];
    const float clampedRadius = SDL_max(radius - t, 1.0f);

    for (int i = 0; i <= numCircleSegments; i++) {
      const float angle = radStart + i * angleStep;
      points[i] =
          (SDL_FPoint){SDL_roundf(center.x + SDL_cosf(angle) * clampedRadius),
                       SDL_roundf(center.y + SDL_sinf(angle) * clampedRadius)};
    }
    SDL_RenderLines(rendererData->renderer, points, numCircleSegments + 1);
  }
}

SDL_Rect currentClippingRectangle;

void SDL_Clay_RenderClayCommands(Clay_SDL3RendererData *rendererData,
                                 Clay_RenderCommandArray *rcommands) {
  g_frame++;   // for the text-cache mark-and-sweep at the end of this frame

  // Logical->output scale of the letterbox presentation. Geometry is GPU-scaled
  // (stays crisp), but glyph atlases are textures: rasterized at the logical
  // font size they'd be upscaled and blur. So we rasterize text at the *device*
  // pixel size (fontSize * pres_scale) and compensate with a render scale of
  // 1/pres_scale, which composes with the presentation transform (see the text
  // case). At native size pres_scale == 1 and this is a no-op.
  float pres_scale = 1.0f;
  {
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode;
    SDL_FRect pres;
    if (SDL_GetRenderLogicalPresentation(rendererData->renderer, &lw, &lh, &mode) &&
        lw > 0 && mode != SDL_LOGICAL_PRESENTATION_DISABLED &&
        SDL_GetRenderLogicalPresentationRect(rendererData->renderer, &pres) &&
        pres.w > 0) {
      pres_scale = pres.w / (float)lw;
    }
  }

  for (size_t i = 0; i < rcommands->length; i++) {
    Clay_RenderCommand *rcmd = Clay_RenderCommandArray_Get(rcommands, i);
    const Clay_BoundingBox bounding_box = rcmd->boundingBox;
    const SDL_FRect rect = {(int)bounding_box.x, (int)bounding_box.y,
                            (int)bounding_box.width, (int)bounding_box.height};

    switch (rcmd->commandType) {
    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
      Clay_RectangleRenderData *config = &rcmd->renderData.rectangle;
      SDL_SetRenderDrawBlendMode(rendererData->renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(rendererData->renderer, config->backgroundColor.r,
                             config->backgroundColor.g,
                             config->backgroundColor.b,
                             config->backgroundColor.a);
      if (config->cornerRadius.topLeft > 0) {
        SDL_Clay_RenderFillRoundedRect(rendererData, rect,
                                       config->cornerRadius.topLeft,
                                       config->backgroundColor);
      } else {
        SDL_RenderFillRect(rendererData->renderer, &rect);
      }
    } break;
    case CLAY_RENDER_COMMAND_TYPE_TEXT: {
      Clay_TextRenderData *config = &rcmd->renderData.text;
      const char *chars = config->stringContents.chars;
      size_t len = config->stringContents.length;

      // Rasterize at device pixels so text stays sharp when the world is
      // scaled. Keying the cache on the device size means a resolution change
      // naturally re-rasterizes at the new size instead of reusing a stale,
      // blurry atlas.
      int px_size = (int)(config->fontSize * pres_scale + 0.5f);
      if (px_size < 1) px_size = 1;
      uint64_t key = text_key(config->fontId, (float)px_size, chars, len);

      // Look the string up in the cache; note a reusable slot while scanning.
      TTF_Text *text = NULL;
      int free_slot = -1, stale_slot = -1;
      for (int k = 0; k < TEXT_CACHE_CAP; k++) {
        if (g_text_cache[k].used) {
          if (g_text_cache[k].key == key) {
            text = g_text_cache[k].text;
            g_text_cache[k].touched = g_frame;
            break;
          }
          if (stale_slot < 0 && g_text_cache[k].touched != g_frame)
            stale_slot = k;
        } else if (free_slot < 0) {
          free_slot = k;
        }
      }

      bool cached = (text != NULL);
      if (!text) {
        // Miss: rasterize once against the (never-resized) sized font, at the
        // device pixel size for crispness when scaled.
        TTF_Font *font = SDL_Clay_GetSizedFont(rendererData, config->fontId,
                                               px_size);
        text = TTF_CreateText(rendererData->textEngine, font, chars, len);
        if (text) {
          int slot = (free_slot >= 0) ? free_slot : stale_slot;
          if (slot >= 0) {
            if (g_text_cache[slot].used && g_text_cache[slot].text)
              TTF_DestroyText(g_text_cache[slot].text);
            g_text_cache[slot] =
                (TextEntry){key, text, g_frame, true};
            cached = true;
          }
        }
      }

      if (text) {
        TTF_SetTextColor(text, config->textColor.r, config->textColor.g,
                         config->textColor.b, config->textColor.a);
        if (pres_scale != 1.0f) {
          // The glyphs are pres_scale x bigger than their logical size; a
          // render scale of 1/pres_scale shrinks them back, and since it
          // composes on top of the presentation scale the net output size is
          // exactly the logical fontSize -- but drawn from a native-res atlas.
          // Positions are in this scaled space, hence rect.* * pres_scale.
          float inv = 1.0f / pres_scale;
          SDL_SetRenderScale(rendererData->renderer, inv, inv);
          TTF_DrawRendererText(text, rect.x * pres_scale, rect.y * pres_scale);
          SDL_SetRenderScale(rendererData->renderer, 1.0f, 1.0f);
        } else {
          TTF_DrawRendererText(text, rect.x, rect.y);
        }
        if (!cached) TTF_DestroyText(text);   // cache saturated: don't leak
      }
    } break;
    case CLAY_RENDER_COMMAND_TYPE_BORDER: {
      Clay_BorderRenderData *config = &rcmd->renderData.border;

      const float minRadius = SDL_min(rect.w, rect.h) / 2.0f;
      const Clay_CornerRadius clampedRadii = {
          .topLeft = SDL_min(config->cornerRadius.topLeft, minRadius),
          .topRight = SDL_min(config->cornerRadius.topRight, minRadius),
          .bottomLeft = SDL_min(config->cornerRadius.bottomLeft, minRadius),
          .bottomRight = SDL_min(config->cornerRadius.bottomRight, minRadius)};
      // edges
      SDL_SetRenderDrawColor(rendererData->renderer, config->color.r,
                             config->color.g, config->color.b, config->color.a);
      if (config->width.left > 0) {
        const float starting_y = rect.y + clampedRadii.topLeft;
        const float length =
            rect.h - clampedRadii.topLeft - clampedRadii.bottomLeft;
        SDL_FRect line = {rect.x - 1, starting_y, config->width.left, length};
        SDL_RenderFillRect(rendererData->renderer, &line);
      }
      if (config->width.right > 0) {
        const float starting_x =
            rect.x + rect.w - (float)config->width.right + 1;
        const float starting_y = rect.y + clampedRadii.topRight;
        const float length =
            rect.h - clampedRadii.topRight - clampedRadii.bottomRight;
        SDL_FRect line = {starting_x, starting_y, config->width.right, length};
        SDL_RenderFillRect(rendererData->renderer, &line);
      }
      if (config->width.top > 0) {
        const float starting_x = rect.x + clampedRadii.topLeft;
        const float length =
            rect.w - clampedRadii.topLeft - clampedRadii.topRight;
        SDL_FRect line = {starting_x, rect.y - 1, length, config->width.top};
        SDL_RenderFillRect(rendererData->renderer, &line);
      }
      if (config->width.bottom > 0) {
        const float starting_x = rect.x + clampedRadii.bottomLeft;
        const float starting_y =
            rect.y + rect.h - (float)config->width.bottom + 1;
        const float length =
            rect.w - clampedRadii.bottomLeft - clampedRadii.bottomRight;
        SDL_FRect line = {starting_x, starting_y, length, config->width.bottom};
        SDL_SetRenderDrawColor(rendererData->renderer, config->color.r,
                               config->color.g, config->color.b,
                               config->color.a);
        SDL_RenderFillRect(rendererData->renderer, &line);
      }
      // corners
      if (config->cornerRadius.topLeft > 0) {
        const float centerX = rect.x + clampedRadii.topLeft - 1;
        const float centerY = rect.y + clampedRadii.topLeft - 1;
        SDL_Clay_RenderArc(rendererData, (SDL_FPoint){centerX, centerY},
                           clampedRadii.topLeft, 180.0f, 270.0f,
                           config->width.top, config->color);
      }
      if (config->cornerRadius.topRight > 0) {
        const float centerX = rect.x + rect.w - clampedRadii.topRight;
        const float centerY = rect.y + clampedRadii.topRight - 1;
        SDL_Clay_RenderArc(rendererData, (SDL_FPoint){centerX, centerY},
                           clampedRadii.topRight, 270.0f, 360.0f,
                           config->width.top, config->color);
      }
      if (config->cornerRadius.bottomLeft > 0) {
        const float centerX = rect.x + clampedRadii.bottomLeft - 1;
        const float centerY = rect.y + rect.h - clampedRadii.bottomLeft;
        SDL_Clay_RenderArc(rendererData, (SDL_FPoint){centerX, centerY},
                           clampedRadii.bottomLeft, 90.0f, 180.0f,
                           config->width.bottom, config->color);
      }
      if (config->cornerRadius.bottomRight > 0) {
        const float centerX = rect.x + rect.w - clampedRadii.bottomRight;
        const float centerY = rect.y + rect.h - clampedRadii.bottomRight;
        SDL_Clay_RenderArc(rendererData, (SDL_FPoint){centerX, centerY},
                           clampedRadii.bottomRight, 0.0f, 90.0f,
                           config->width.bottom, config->color);
      }

    } break;
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
      Clay_BoundingBox boundingBox = rcmd->boundingBox;
      currentClippingRectangle = (SDL_Rect){
          .x = boundingBox.x,
          .y = boundingBox.y,
          .w = boundingBox.width,
          .h = boundingBox.height,
      };
      SDL_SetRenderClipRect(rendererData->renderer, &currentClippingRectangle);
      break;
    }
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
      SDL_SetRenderClipRect(rendererData->renderer, NULL);
      break;
    }
    case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
      SDL_Texture *texture = (SDL_Texture *)rcmd->renderData.image.imageData;
      const SDL_FRect dest = {rect.x, rect.y, rect.w, rect.h};
      SDL_RenderTexture(rendererData->renderer, texture, NULL, &dest);
      break;
    }
    default:
      SDL_Log("Unknown render command type: %d", rcmd->commandType);
    }
  }

  // Mark-and-sweep: drop cached text that did not appear this frame (e.g. a
  // previous FPS/score value), so the cache tracks only live strings.
  for (int k = 0; k < TEXT_CACHE_CAP; k++)
    if (g_text_cache[k].used && g_text_cache[k].touched != g_frame) {
      TTF_DestroyText(g_text_cache[k].text);
      g_text_cache[k].text = NULL;
      g_text_cache[k].used = false;
    }
}
