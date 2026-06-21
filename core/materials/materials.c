#include "materials.h"

#include <math.h>
#include <string.h>

int material_from_name(const char *name) {
  if (!name) return MAT_FLAT;
  if (!strcmp(name, "holo"))   return MAT_HOLO;
  if (!strcmp(name, "chrome")) return MAT_CHROME;
  if (!strcmp(name, "glass"))  return MAT_GLASS;
  return MAT_FLAT;
}

static float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

// HSV (h wrapped to [0,1)) -> RGB, each 0..1. Used by the holographic material.
static void hsv2rgb(float h, float s, float v, float *r, float *g, float *b) {
  h -= floorf(h);
  float i = floorf(h * 6.0f), f = h * 6.0f - i;
  float p = v * (1 - s), q = v * (1 - s * f), tt = v * (1 - s * (1 - f));
  switch (((int)i) % 6) {
    case 0:  *r = v;  *g = tt; *b = p;  break;
    case 1:  *r = q;  *g = v;  *b = p;  break;
    case 2:  *r = p;  *g = v;  *b = tt; break;
    case 3:  *r = p;  *g = q;  *b = v;  break;
    case 4:  *r = tt; *g = p;  *b = v;  break;
    default: *r = v;  *g = p;  *b = q;  break;
  }
}

// ---- per-material fragment functions --------------------------------------
// (u,v) run 0..1 over the face; vx/vy are the view tilt. Each writes linear
// 0..1 colour (and alpha for glass) before the shared frame/dim pass.

static void frag_holo(float u, float v, float vx, float vy, float t,
                      float *r, float *g, float *b, float *a) {
  float hue  = 0.5f * (u + v) + 0.8f * (vx - 0.6f * vy) + 0.04f * t;
  float band = sinf((u - v) * 20.0f + vx * 9.0f - t * 1.4f) * 0.5f + 0.5f;
  hsv2rgb(hue, 0.55f, 0.62f + 0.38f * band, r, g, b);
  float sp = 0.5f * (u + v) - (0.5f + vx * 0.6f);   // sliding sheen streak
  float sheen = expf(-sp * sp * 60.0f);
  *r += sheen * 0.5f; *g += sheen * 0.5f; *b += sheen * 0.5f;
  *a = 1.0f;
}

static void frag_chrome(float u, float v, float vx, float vy, float t,
                        float *r, float *g, float *b, float *a) {
  (void)t;
  // Vertical environment reflection that slides with the tilt, plus a bright
  // "horizon" streak -> a polished-metal read.
  float refl = clampf(0.5f + (v - 0.5f) * 1.5f + vy * 1.4f
                        + (u - 0.5f) * vx * 0.6f, 0.0f, 1.0f);
  float metal = 0.25f + 0.5f * refl;
  float hb = expf(-powf((refl - 0.60f) / 0.07f, 2.0f));
  metal = clampf(metal + 0.5f * hb, 0.0f, 1.1f);
  *r = metal * 0.92f; *g = metal * 0.96f; *b = metal;
  *a = 1.0f;
}

static void frag_glass(float u, float v, float vx, float vy, float t,
                       float *r, float *g, float *b, float *a) {
  (void)t;
  // Translucent, bright fresnel rim + a specular highlight that slides w/ tilt.
  float edge = fminf(fminf(u, 1.0f - u), fminf(v, 1.0f - v));
  float rim  = clampf(1.0f - edge * 7.0f, 0.0f, 1.0f);
  float sx0 = 0.5f + vx * 0.7f, sy0 = 0.45f - vy * 0.7f;
  float ddx = u - sx0, ddy = v - sy0;
  float spec = expf(-(ddx * ddx + ddy * ddy) * 22.0f);
  *r = 0.45f + 0.40f * spec + rim * 0.5f;
  *g = 0.60f + 0.40f * spec + rim * 0.5f;
  *b = 0.72f + 0.40f * spec + rim * 0.5f;
  *a = clampf(0.42f + 0.5f * rim + 0.4f * spec, 0.0f, 1.0f);
}

void material_shade(Uint32 *pixels, int pitch, int material,
                    float vx, float vy, float facing, float t) {
  int stride = pitch / 4;
  float aspect = (float)CARD_TEX_W / (float)CARD_TEX_H;
  float k = 0.55f + 0.45f * facing;          // edge-on dimming

  for (int iy = 0; iy < CARD_TEX_H; iy++) {
    float v = (float)iy / (CARD_TEX_H - 1);
    Uint32 *prow = pixels + iy * stride;
    for (int ix = 0; ix < CARD_TEX_W; ix++) {
      float u = (float)ix / (CARD_TEX_W - 1);

      // Rounded-rect alpha mask (transparent corners).
      float dx = fminf(u, 1.0f - u), dy = fminf(v, 1.0f - v);
      float ru = 0.09f, rv = 0.09f * aspect;
      if (dx < ru && dy < rv) {
        float nx = (ru - dx) / ru, ny = (rv - dy) / rv;
        if (nx * nx + ny * ny > 1.0f) { prow[ix] = 0; continue; }
      }

      float r, g, b, a;
      if      (material == MAT_HOLO)   frag_holo(u, v, vx, vy, t, &r, &g, &b, &a);
      else if (material == MAT_CHROME) frag_chrome(u, v, vx, vy, t, &r, &g, &b, &a);
      else                             frag_glass(u, v, vx, vy, t, &r, &g, &b, &a);

      // Baked frame just inside the edge.
      float em = fminf(fminf(u, 1.0f - u), fminf(v, 1.0f - v));
      if (em < 0.022f) { r = g = b = 0.95f; a = 1.0f; }

      r *= k; g *= k; b *= k;
      Uint8 R = (Uint8)(clampf(r, 0, 1) * 255), G = (Uint8)(clampf(g, 0, 1) * 255),
            B = (Uint8)(clampf(b, 0, 1) * 255), A = (Uint8)(clampf(a, 0, 1) * 255);
      prow[ix] = ((Uint32)A << 24) | ((Uint32)R << 16) | ((Uint32)G << 8) | B;
    }
  }
}
