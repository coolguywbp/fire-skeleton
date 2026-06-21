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

// Cheap stand-ins for the transcendentals on the hot per-pixel path (this runs
// CARD_TEX_W*CARD_TEX_H times per re-shade). bump() ≈ a Gaussian highlight
// exp(-k·d²) via a rational falloff; wave01() ≈ (sin(ph)+1)/2 via a smoothed
// triangle. Visually close, far cheaper than expf/sinf/powf.
static float bump(float d2, float k) { return 1.0f / (1.0f + k * d2); }

static float wave01(float ph) {
  ph *= 0.15915494f;                          // / (2π) -> turns
  ph -= floorf(ph);                           // 0..1
  float tri = 1.0f - fabsf(2.0f * ph - 1.0f);
  return tri * tri * (3.0f - 2.0f * tri);     // smoothstep -> rounded like a sine
}

// ---- per-material fragment functions --------------------------------------
// (u,v) run 0..1 over the face; vx/vy are the view tilt. `hb` flags whether a
// card-art base colour (br,bg,bb) is present: with art the material blends OVER
// it (foil / reflection / glass), without it paints a procedural surface. Each
// writes linear 0..1 colour (+ alpha).

static void frag_holo(float u, float v, float vx, float vy, float t, int hb,
                      float br, float bg, float bb,
                      float *r, float *g, float *b, float *a) {
  (void)t;   // the rainbow shifts with the tilt, not on a timer (lets idle cards
             // stay cached instead of re-shading every frame)
  float hue  = 0.5f * (u + v) + 0.8f * (vx - 0.6f * vy);
  float band = wave01((u - v) * 20.0f + vx * 9.0f);
  float sp = 0.5f * (u + v) - (0.5f + vx * 0.6f);   // sliding sheen streak
  float sheen = bump(sp * sp, 60.0f);
  if (hb) {
    // Foil over the art: additive rainbow that pools into the bands, plus sheen.
    float hr, hg, hbl; hsv2rgb(hue, 0.9f, 1.0f, &hr, &hg, &hbl);
    float foil = 0.10f + 0.30f * band;
    *r = br + hr * foil + sheen * 0.45f;
    *g = bg + hg * foil + sheen * 0.45f;
    *b = bb + hbl * foil + sheen * 0.45f;
  } else {
    hsv2rgb(hue, 0.55f, 0.62f + 0.38f * band, r, g, b);
    *r += sheen * 0.5f; *g += sheen * 0.5f; *b += sheen * 0.5f;
  }
  *a = 1.0f;
}

static void frag_chrome(float u, float v, float vx, float vy, float t, int hb,
                        float br, float bg, float bb,
                        float *r, float *g, float *b, float *a) {
  (void)t;
  // Vertical environment reflection that slides with the tilt, plus a bright
  // "horizon" streak -> a polished-metal read.
  float refl = clampf(0.5f + (v - 0.5f) * 1.5f + vy * 1.4f
                        + (u - 0.5f) * vx * 0.6f, 0.0f, 1.0f);
  float dz = refl - 0.60f;
  float streak = bump(dz * dz, 204.0f);          // exp(-((refl-0.6)/0.07)^2)
  if (hb) {
    // Chrome-plated art: keep the picture readable, lay a cool metal sheen on top.
    float sheen = 0.20f + 0.45f * refl;
    *r = (br * 0.70f + sheen * 0.34f) * 0.94f + streak * 0.5f;
    *g = (bg * 0.70f + sheen * 0.34f) * 0.97f + streak * 0.5f;
    *b = (bb * 0.70f + sheen * 0.34f)         + streak * 0.5f;
  } else {
    float metal = clampf(0.25f + 0.5f * refl + 0.5f * streak, 0.0f, 1.1f);
    *r = metal * 0.92f; *g = metal * 0.96f; *b = metal;
  }
  *a = 1.0f;
}

static void frag_glass(float u, float v, float vx, float vy, float t, int hb,
                       float br, float bg, float bb,
                       float *r, float *g, float *b, float *a) {
  (void)t;
  // Bright fresnel rim + a specular highlight that slides with the tilt.
  float edge = fminf(fminf(u, 1.0f - u), fminf(v, 1.0f - v));
  float rim  = clampf(1.0f - edge * 7.0f, 0.0f, 1.0f);
  float sx0 = 0.5f + vx * 0.7f, sy0 = 0.45f - vy * 0.7f;
  float ddx = u - sx0, ddy = v - sy0;
  float spec = bump(ddx * ddx + ddy * ddy, 22.0f);
  if (hb) {
    // Glass over the art: art stays visible, edges glow, a highlight slides by.
    *r = br + rim * 0.5f + spec * 0.7f;
    *g = bg + rim * 0.5f + spec * 0.7f;
    *b = bb + rim * 0.6f + spec * 0.7f;
    *a = 1.0f;
  } else {
    *r = 0.45f + 0.40f * spec + rim * 0.5f;
    *g = 0.60f + 0.40f * spec + rim * 0.5f;
    *b = 0.72f + 0.40f * spec + rim * 0.5f;
    *a = clampf(0.42f + 0.5f * rim + 0.4f * spec, 0.0f, 1.0f);
  }
}

void material_shade(Uint32 *pixels, int pitch, int material,
                    float vx, float vy, float facing, float t,
                    const Uint32 *base, int base_w, int base_h, int base_stride) {
  (void)base_w; (void)base_h;   // base is already at this texture's resolution
  int stride = pitch / 4;
  int has_base = base != NULL;
  float aspect = (float)CARD_TEX_W / (float)CARD_TEX_H;
  float k = 0.55f + 0.45f * facing;          // edge-on dimming
  const float inv_w = 1.0f / (CARD_TEX_W - 1), inv_h = 1.0f / (CARD_TEX_H - 1);
  const float inv255 = 1.0f / 255.0f;

  for (int iy = 0; iy < CARD_TEX_H; iy++) {
    float v = iy * inv_h;
    Uint32 *prow = pixels + iy * stride;
    const Uint32 *brow = has_base ? base + iy * base_stride : NULL;
    for (int ix = 0; ix < CARD_TEX_W; ix++) {
      float u = ix * inv_w;

      // Soft rounded-rect alpha mask. A hard 0/1 cutoff makes the card's edges
      // (and rounded corners) jagged when it tilts -- SDL doesn't anti-alias the
      // quad boundary. Instead the opaque region is inset slightly and the alpha
      // ramps to 0 over a couple of texels along the WHOLE outline, so the
      // visible edge is this smooth ramp, not the polygon edge.
      const float ru = 0.05f, rv = 0.05f * aspect, aa = 0.012f;
      float dx = fminf(u, 1.0f - u), dy = fminf(v, 1.0f - v);
      float ea;   // edge alpha: 0 outside -> 1 inside, AA across `aa`
      if (dx < ru && dy < rv) {                       // rounded corner: radial
        float nx = (ru - dx) / ru, ny = (rv - dy) / rv;
        ea = clampf((1.0f - sqrtf(nx * nx + ny * ny)) * ru / aa, 0.0f, 1.0f);
      } else {                                        // straight edge
        ea = clampf(fminf(dx, dy) / aa, 0.0f, 1.0f);
      }
      if (ea <= 0.0f) { prow[ix] = 0; continue; }

      // The art is pre-resampled to this texture's size, so read it 1:1.
      float br = 0, bg = 0, bb = 0;
      if (has_base) {
        Uint32 bp = brow[ix];
        br = ((bp >> 16) & 0xff) * inv255;
        bg = ((bp >> 8)  & 0xff) * inv255;
        bb = ( bp        & 0xff) * inv255;
      }

      float r, g, b, a;
      if      (material == MAT_HOLO)   frag_holo(u, v, vx, vy, t, has_base, br, bg, bb, &r, &g, &b, &a);
      else if (material == MAT_CHROME) frag_chrome(u, v, vx, vy, t, has_base, br, bg, bb, &r, &g, &b, &a);
      else                             frag_glass(u, v, vx, vy, t, has_base, br, bg, bb, &r, &g, &b, &a);

      // Synthetic frame only when there's no art (the art carries its own).
      if (!has_base) {
        float em = fminf(fminf(u, 1.0f - u), fminf(v, 1.0f - v));
        if (em < 0.022f) { r = g = b = 0.95f; a = 1.0f; }
      }

      r *= k; g *= k; b *= k;
      Uint8 R = (Uint8)(clampf(r, 0, 1) * 255), G = (Uint8)(clampf(g, 0, 1) * 255),
            B = (Uint8)(clampf(b, 0, 1) * 255), A = (Uint8)(clampf(a * ea, 0, 1) * 255);
      prow[ix] = ((Uint32)A << 24) | ((Uint32)R << 16) | ((Uint32)G << 8) | B;
    }
  }
}
