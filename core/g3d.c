#include "g3d.h"
#include "game.h"               // g_logical_w/h + SDL types (via main.h)
#include "materials/materials.h"  // card materials (per-pixel shaders)

#include <SDL3_image/SDL_image.h>  // IMG_Load (card art surfaces)
#include <lua.h>
#include <lauxlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Perspective camera. The camera sits at g_cam_* looking along +z; the focal
// length is derived from the vertical field of view, so the scene controls
// framing through g3d.camera()/g3d.fov() instead of fixed magic numbers. The
// defaults put the camera 4 units back with a ~58 degrees FOV, which matches the
// previous fixed projection. Resolution independence still comes from tying the
// focal to the logical height (the logical presentation handles the letterbox).
#define G3D_PI 3.14159265358979323846f

static float g_cam_x = 0.0f, g_cam_y = 0.0f, g_cam_z = -4.0f;  // camera position
static float g_fov_deg = 58.0f;                                // vertical FOV

// Projected line list, filled by the g3d.* primitives during on_ui() and drawn
// by g3d_flush() in the render pass. Lines hold final 2D (logical) coordinates;
// all the 3D work is already done by the time they land here.
#define G3D_MAX_LINES 8192
typedef struct { float x1, y1, x2, y2; Uint8 r, g, b, a; float w; } G3DLine;
static G3DLine g_lines[G3D_MAX_LINES];
static int     g_nlines;

// Filled triangles hold final 2D (logical) positions and a per-triangle colour
// (already shaded). Drawn before the lines so wireframe edges sit on top.
#define G3D_MAX_TRIS 4096
typedef struct { SDL_FPoint p[3]; SDL_FColor c; } G3DTri;
static G3DTri g_tris[G3D_MAX_TRIS];
static int    g_ntris;

// Shaded cards: a flat quad whose surface is painted by a per-pixel material
// (holographic / chrome / glass, see materials/) into a streaming texture, then
// drawn as a perspective quad. The command records the four projected corners
// plus the shader uniforms (rotation -> view tilt, facing) so the material
// shifts as the card tilts. Shading happens in g3d_flush (it needs the renderer
// to make/lock the texture).
#define G3D_MAX_CARDS 64
// The card face is drawn as a subdivided grid (not one quad) so the textured
// material maps near-perspective-correct -- a single tilted quad would shear the
// picture (SDL_RenderGeometry interpolates UVs affinely).
#define G3D_GX 5
#define G3D_GY 7
#define G3D_GRIDV ((G3D_GX + 1) * (G3D_GY + 1))
typedef struct {
  SDL_FPoint   p[4];               // projected corners: TL, TR, BR, BL (reflection)
  float        grid[G3D_GRIDV][2]; // projected grid vertices (face)
  int          material;
  float        rx, ry;             // tilt -> view-dependent shading + cache key
  float        facing;             // 0..1, how square-on to the camera (dims)
  SDL_Surface *base;               // optional card-art image, NULL = procedural
} G3DCard;
static G3DCard      g_cards[G3D_MAX_CARDS];
static int          g_ncards;
static SDL_Texture *g_card_pool[G3D_MAX_CARDS];   // lazily created, one per slot

// Per-slot record of what each texture was last shaded with, so an unchanging
// card (idle, or held still) is not re-shaded every frame. Persists across
// frames on purpose.
typedef struct { int material; float rx, ry; const void *base; int valid; } CardCache;
static CardCache g_card_cache[G3D_MAX_CARDS];

void g3d_begin_frame(void) { g_nlines = 0; g_ntris = 0; g_ncards = 0; }

// Card-art cache: load assets/images/<name>.png once as a CPU surface (ARGB8888)
// so a material can sample it per pixel. Cached by name (NULL result cached too,
// so a missing file isn't retried every frame).
#define G3D_MAX_ART 16
static struct { char name[48]; SDL_Surface *surf; } g_art[G3D_MAX_ART];
static int g_nart;

static SDL_Surface *load_card_art(const char *name) {
  for (int i = 0; i < g_nart; i++)
    if (!strcmp(g_art[i].name, name)) return g_art[i].surf;
  if (g_nart >= G3D_MAX_ART) return NULL;

  char path[128];
  snprintf(path, sizeof path, "assets/images/%s.png", name);
  SDL_Surface *s = IMG_Load(path), *conv = NULL;
  if (s) {
    conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(s);
  }
  snprintf(g_art[g_nart].name, sizeof g_art[g_nart].name, "%s", name);
  g_art[g_nart].surf = conv;
  g_nart++;
  return conv;
}

// Per-slot art pre-resampled to the card-texture size, so the per-frame shade
// reads it 1:1 instead of bilinear-sampling the full-res art every pixel. Built
// once per (slot, source) and reused.
static Uint32     *g_base_res[G3D_MAX_CARDS];
static const void *g_base_res_src[G3D_MAX_CARDS];

static const Uint32 *ensure_base_res(int slot, SDL_Surface *src) {
  if (!src) return NULL;
  if (g_base_res_src[slot] == src && g_base_res[slot]) return g_base_res[slot];
  if (!g_base_res[slot]) {
    g_base_res[slot] = malloc((size_t)CARD_TEX_W * CARD_TEX_H * sizeof(Uint32));
    if (!g_base_res[slot]) return NULL;
  }
  const Uint32 *sp = (const Uint32 *)src->pixels;
  int ss = src->pitch / 4, sw = src->w, sh = src->h;
  for (int y = 0; y < CARD_TEX_H; y++) {
    float fy = (float)y / (CARD_TEX_H - 1) * (sh - 1);
    int y0 = (int)fy, y1 = y0 + 1 < sh ? y0 + 1 : y0; float ty = fy - y0;
    for (int x = 0; x < CARD_TEX_W; x++) {
      float fx = (float)x / (CARD_TEX_W - 1) * (sw - 1);
      int x0 = (int)fx, x1 = x0 + 1 < sw ? x0 + 1 : x0; float tx = fx - x0;
      Uint32 p00 = sp[y0*ss+x0], p10 = sp[y0*ss+x1], p01 = sp[y1*ss+x0], p11 = sp[y1*ss+x1];
      Uint32 out = 0xff000000u;
      for (int ch = 0; ch < 3; ch++) {
        int sh2 = ch * 8;
        float c00 = (p00>>sh2)&0xff, c10 = (p10>>sh2)&0xff,
              c01 = (p01>>sh2)&0xff, c11 = (p11>>sh2)&0xff;
        float top = c00 + (c10-c00)*tx, bot = c01 + (c11-c01)*tx;
        out |= (Uint32)(top + (bot-top)*ty + 0.5f) << sh2;
      }
      g_base_res[slot][y*CARD_TEX_W + x] = out;
    }
  }
  g_base_res_src[slot] = src;
  return g_base_res[slot];
}

// Lazily create the streaming texture for card slot i.
static SDL_Texture *card_texture(SDL_Renderer *r, int i) {
  if (i < 0 || i >= G3D_MAX_CARDS) return NULL;
  if (!g_card_pool[i]) {
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       CARD_TEX_W, CARD_TEX_H);
    if (t) {
      SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    }
    g_card_pool[i] = t;
  }
  return g_card_pool[i];
}

void g3d_flush(struct SDL_Renderer *renderer) {
  for (int i = 0; i < g_ntris; i++) {
    G3DTri *t = &g_tris[i];
    SDL_Vertex v[3] = {
      { t->p[0], t->c, { 0, 0 } },
      { t->p[1], t->c, { 0, 0 } },
      { t->p[2], t->c, { 0, 0 } },
    };
    SDL_RenderGeometry(renderer, NULL, v, 3, NULL, 0);
  }
  for (int i = 0; i < g_nlines; i++) {
    G3DLine *l = &g_lines[i];
    SDL_SetRenderDrawColor(renderer, l->r, l->g, l->b, l->a);
    int reps = (int)(l->w + 0.5f);
    if (reps <= 1) {
      SDL_RenderLine(renderer, l->x1, l->y1, l->x2, l->y2);
    } else {
      // No thick-line primitive in SDL; fake width with parallel copies offset
      // along the segment's perpendicular.
      float dx = l->x2 - l->x1, dy = l->y2 - l->y1;
      float len = SDL_sqrtf(dx * dx + dy * dy);
      float nx = len > 0.0f ? -dy / len : 0.0f;
      float ny = len > 0.0f ?  dx / len : 0.0f;
      for (int k = 0; k < reps; k++) {
        float off = (float)k - (reps - 1) / 2.0f;
        SDL_RenderLine(renderer, l->x1 + nx * off, l->y1 + ny * off,
                                 l->x2 + nx * off, l->y2 + ny * off);
      }
    }
  }

  // Shaded cards last (drawn in submission order so the scene controls overlap).
  float t = (float)SDL_GetTicks() / 1000.0f;
  SDL_FColor white = { 1, 1, 1, 1 };
  for (int i = 0; i < g_ncards; i++) {
    G3DCard *c = &g_cards[i];
    SDL_Texture *tex = card_texture(renderer, i);
    if (!tex) continue;

    // Re-shade only when an input that affects the pixels changed (tilt / facing
    // / material / art). Idle cards keep last frame's texture -> big FPS win.
    // Quantise the tilt so small drifts don't re-shade: the texture only changes
    // visibly past a step, and at high frame rates the per-frame tilt delta is
    // tiny -- so most frames stay cached and the cost is paid only occasionally.
    CardCache *ca = &g_card_cache[i];
    int dirty = !ca->valid || ca->material != c->material || ca->base != c->base
                || fabsf(ca->rx - c->rx) > 6e-3f || fabsf(ca->ry - c->ry) > 6e-3f;
    if (dirty) {
      const Uint32 *bpx = ensure_base_res(i, c->base);
      void *px; int pitch;
      if (SDL_LockTexture(tex, NULL, &px, &pitch)) {
        // vx/vy are the view tilt: the card's own ry/rx. base is pre-resampled
        // to the texture size, so it's read 1:1.
        material_shade((Uint32 *)px, pitch, c->material, c->ry, c->rx, c->facing, t,
                       bpx, CARD_TEX_W, CARD_TEX_H, CARD_TEX_W);
        SDL_UnlockTexture(tex);
      }
      ca->valid = 1; ca->material = c->material; ca->base = c->base;
      ca->rx = c->rx; ca->ry = c->ry;
    }

    // Faded reflection below the card: mirror the top corners across the bottom
    // edge (one quad is fine -- it's faded and slightly skewed anyway). Reads as
    // depth on the black backdrop where a dark drop-shadow could not.
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_FPoint r0 = { 2 * c->p[3].x - c->p[0].x, 2 * c->p[3].y - c->p[0].y };
    SDL_FPoint r1 = { 2 * c->p[2].x - c->p[1].x, 2 * c->p[2].y - c->p[1].y };
    SDL_FColor near = { 1, 1, 1, 0.28f }, far = { 1, 1, 1, 0.0f };
    SDL_Vertex rv[4] = {
      { c->p[3], near, { 0, 1 } }, { c->p[2], near, { 1, 1 } },
      { r1,      far,  { 1, 0 } }, { r0,      far,  { 0, 0 } },
    };
    SDL_RenderGeometry(renderer, tex, rv, 4, idx, 6);

    // The card face, as a subdivided grid so the texture stays straight when the
    // card tilts (no affine warp of the picture/text).
    SDL_Vertex v[G3D_GRIDV];
    for (int gj = 0; gj <= G3D_GY; gj++)
      for (int gi = 0; gi <= G3D_GX; gi++) {
        int id = gj * (G3D_GX + 1) + gi;
        v[id].position  = (SDL_FPoint){ c->grid[id][0], c->grid[id][1] };
        v[id].color     = white;
        v[id].tex_coord = (SDL_FPoint){ (float)gi / G3D_GX, (float)gj / G3D_GY };
      }
    int gidx[G3D_GX * G3D_GY * 6], ni = 0;
    for (int gj = 0; gj < G3D_GY; gj++)
      for (int gi = 0; gi < G3D_GX; gi++) {
        int a = gj * (G3D_GX + 1) + gi, b = a + 1, cc = a + (G3D_GX + 1), d = cc + 1;
        gidx[ni++] = a; gidx[ni++] = b; gidx[ni++] = d;
        gidx[ni++] = a; gidx[ni++] = d; gidx[ni++] = cc;
      }
    SDL_RenderGeometry(renderer, tex, v, G3D_GRIDV, gidx, ni);
  }
}

// ---------------------------------------------------------------------------
// 3D maths
// ---------------------------------------------------------------------------

typedef struct { float x, y, z; } Vec3;

// Rotate about the origin by Euler angles (X then Y then Z).
static Vec3 rotate(Vec3 p, float rx, float ry, float rz) {
  float cx = cosf(rx), sx = sinf(rx);
  float cy = cosf(ry), sy = sinf(ry);
  float cz = cosf(rz), sz = sinf(rz);
  // X axis
  float y1 = p.y * cx - p.z * sx;
  float z1 = p.y * sx + p.z * cx;
  float x1 = p.x;
  // Y axis
  float x2 =  x1 * cy + z1 * sy;
  float z2 = -x1 * sy + z1 * cy;
  float y2 =  y1;
  // Z axis
  float x3 = x2 * cz - y2 * sz;
  float y3 = x2 * sz + y2 * cz;
  float z3 = z2;
  return (Vec3){ x3, y3, z3 };
}

// Rotate p about a unit axis k by angle with cos = c, sin = s (Rodrigues). Used
// for the card's single-axis, no-roll tilt.
static Vec3 rotate_axis(Vec3 p, Vec3 k, float c, float s) {
  float kd = k.x * p.x + k.y * p.y + k.z * p.z;          // k . p
  Vec3  kc = { k.y * p.z - k.z * p.y,                    // k x p
               k.z * p.x - k.x * p.z,
               k.x * p.y - k.y * p.x };
  return (Vec3){ p.x * c + kc.x * s + k.x * kd * (1.0f - c),
                 p.y * c + kc.y * s + k.y * kd * (1.0f - c),
                 p.z * c + kc.z * s + k.z * kd * (1.0f - c) };
}

// Focal length (in logical pixels) from the vertical FOV: half the viewport
// height over tan(halfFov). Wider FOV -> shorter focal -> smaller, more
// "wide-angle" objects; narrower FOV zooms in.
static float focal_len(void) {
  float fov = g_fov_deg * (G3D_PI / 180.0f);
  return (float)g_logical_h * 0.5f / tanf(fov * 0.5f);
}

// World point -> 2D logical-screen coordinates (perspective projection). The
// point is taken into camera space (relative to the camera position) and then
// divided by its depth.
static void project(Vec3 p, float *sx, float *sy) {
  float cx = p.x - g_cam_x;
  float cy = p.y - g_cam_y;
  float cz = p.z - g_cam_z;
  if (cz < 0.001f) cz = 0.001f;           // never divide through the camera plane
  float focal = focal_len();
  *sx = (float)g_logical_w * 0.5f + (cx / cz) * focal;
  *sy = (float)g_logical_h * 0.5f - (cy / cz) * focal;   // +y is up
}

static void push_line(float x1, float y1, float x2, float y2,
                      Uint8 r, Uint8 g, Uint8 b, Uint8 a, float w) {
  if (g_nlines >= G3D_MAX_LINES) return;
  g_lines[g_nlines++] = (G3DLine){ x1, y1, x2, y2, r, g, b, a, w };
}

static void push_tri(float ax, float ay, float bx, float by, float cx, float cy,
                     SDL_FColor col) {
  if (g_ntris >= G3D_MAX_TRIS) return;
  g_tris[g_ntris++] = (G3DTri){ { { ax, ay }, { bx, by }, { cx, cy } }, col };
}

// Lambert shade: a fixed directional light, plus ambient so unlit faces aren't
// black. `n` is the (unit) outward normal in rotated space; base is 0-255 RGBA.
static SDL_FColor shade(const Uint8 base[4], Vec3 n) {
  static const float Lx = 0.259f, Ly = 0.432f, Lz = -0.864f;  // toward the light
  float nl = n.x * Lx + n.y * Ly + n.z * Lz;
  if (nl < 0.0f) nl = 0.0f;
  float k = 0.30f + 0.70f * nl;                                // ambient + diffuse
  return (SDL_FColor){ base[0] / 255.0f * k, base[1] / 255.0f * k,
                       base[2] / 255.0f * k, base[3] / 255.0f };
}

// ---------------------------------------------------------------------------
// Lua option helpers
// ---------------------------------------------------------------------------

// opts[key] = {r,g,b,a} (0-255); fills `out` with the given default first.
static void opt_rgba_key(lua_State *L, int t, const char *key, Uint8 out[4],
                         Uint8 dr, Uint8 dg, Uint8 db, Uint8 da) {
  out[0] = dr; out[1] = dg; out[2] = db; out[3] = da;
  if (!t) return;
  lua_getfield(L, t, key);
  if (lua_istable(L, -1)) {
    int idx = lua_gettop(L);
    for (int i = 0; i < 4; i++) {
      lua_rawgeti(L, idx, i + 1);
      if (lua_isnumber(L, -1)) out[i] = (Uint8)lua_tointeger(L, -1);
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
}

// opts.color = {r,g,b,a} (0-255); defaults to opaque white.
static void opt_rgba(lua_State *L, int t, Uint8 out[4]) {
  opt_rgba_key(L, t, "color", out, 255, 255, 255, 255);
}

static float opt_f(lua_State *L, int t, const char *k, float def) {
  if (!t) return def;
  lua_getfield(L, t, k);
  float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
  lua_pop(L, 1);
  return v;
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

// g3d.line(x1,y1,z1, x2,y2,z2 [, opts])
static int l_line(lua_State *L) {
  Vec3 a = { (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
  Vec3 b = { (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6) };
  int t = lua_istable(L, 7) ? 7 : 0;
  Uint8 c[4]; opt_rgba(L, t, c);
  float w  = opt_f(L, t, "width", 2);
  float rx = opt_f(L, t, "rx", 0), ry = opt_f(L, t, "ry", 0), rz = opt_f(L, t, "rz", 0);
  a = rotate(a, rx, ry, rz);
  b = rotate(b, rx, ry, rz);
  float ax, ay, bx, by;
  project(a, &ax, &ay);
  project(b, &bx, &by);
  push_line(ax, ay, bx, by, c[0], c[1], c[2], c[3], w);
  return 0;
}

// g3d.cube(cx,cy,cz, size [, opts]) -- rotated about its own centre.
// opts.fill draws shaded solid faces; opts.wire (default true) draws edges.
static int l_cube(lua_State *L) {
  float cx = (float)luaL_checknumber(L, 1);
  float cy = (float)luaL_checknumber(L, 2);
  float cz = (float)luaL_checknumber(L, 3);
  float size = (float)luaL_checknumber(L, 4);
  int t = lua_istable(L, 5) ? 5 : 0;
  Uint8 c[4]; opt_rgba(L, t, c);
  float w  = opt_f(L, t, "width", 2);
  float rx = opt_f(L, t, "rx", 0), ry = opt_f(L, t, "ry", 0), rz = opt_f(L, t, "rz", 0);
  bool fill = false, wire = true;
  if (t) {
    lua_getfield(L, t, "fill"); fill = lua_toboolean(L, -1); lua_pop(L, 1);
    lua_getfield(L, t, "wire");
    if (!lua_isnil(L, -1)) wire = lua_toboolean(L, -1);
    lua_pop(L, 1);
  }

  static const int S[8][3] = {
    {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},   // back face  (z = -1)
    {-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1},   // front face (z = +1)
  };
  static const int E[12][2] = {
    {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7},
  };
  float h = size * 0.5f;
  Vec3  R[8];        // rotated vertices (before camera translate) -- for depth
  float P[8][2];     // projected screen positions
  for (int i = 0; i < 8; i++) {
    Vec3 v = { S[i][0] * h, S[i][1] * h, S[i][2] * h };
    R[i] = rotate(v, rx, ry, rz);
    Vec3 world = { R[i].x + cx, R[i].y + cy, R[i].z + cz };
    project(world, &P[i][0], &P[i][1]);
  }

  if (fill) {
    // Six quad faces, each with its outward axis (rotated -> world normal).
    static const int   F[6][4] = {
      {4,5,6,7}, {0,3,2,1}, {1,2,6,5}, {0,4,7,3}, {3,7,6,2}, {0,1,5,4},
    };
    static const float FN[6][3] = {
      {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0},
    };
    // Painter's order: draw far faces first (sort by centre depth, descending).
    int    order[6] = { 0, 1, 2, 3, 4, 5 };
    float  depth[6];
    for (int f = 0; f < 6; f++) {
      float zc = 0.0f;
      for (int k = 0; k < 4; k++) zc += R[F[f][k]].z;
      depth[f] = zc * 0.25f + cz;             // depth along +z (camera z is a constant offset)
    }
    for (int a = 0; a < 6; a++)               // insertion sort, far -> near
      for (int b = a + 1; b < 6; b++)
        if (depth[order[b]] > depth[order[a]]) { int tmp = order[a]; order[a] = order[b]; order[b] = tmp; }

    for (int o = 0; o < 6; o++) {
      int f = order[o];
      Vec3 n = rotate((Vec3){ FN[f][0], FN[f][1], FN[f][2] }, rx, ry, rz);
      SDL_FColor col = shade(c, n);
      int *q = (int *)F[f];
      push_tri(P[q[0]][0], P[q[0]][1], P[q[1]][0], P[q[1]][1], P[q[2]][0], P[q[2]][1], col);
      push_tri(P[q[0]][0], P[q[0]][1], P[q[2]][0], P[q[2]][1], P[q[3]][0], P[q[3]][1], col);
    }
  }

  if (wire)
    for (int e = 0; e < 12; e++)
      push_line(P[E[e][0]][0], P[E[e][0]][1], P[E[e][1]][0], P[E[e][1]][1],
                c[0], c[1], c[2], c[3], w);
  return 0;
}

// g3d.tri(x1,y1,z1, x2,y2,z2, x3,y3,z3 [, opts]) -- flat filled 3D triangle.
static int l_tri(lua_State *L) {
  Vec3 a = { (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
  Vec3 b = { (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6) };
  Vec3 d = { (float)luaL_checknumber(L, 7), (float)luaL_checknumber(L, 8), (float)luaL_checknumber(L, 9) };
  int t = lua_istable(L, 10) ? 10 : 0;
  Uint8 c[4]; opt_rgba(L, t, c);
  float rx = opt_f(L, t, "rx", 0), ry = opt_f(L, t, "ry", 0), rz = opt_f(L, t, "rz", 0);
  a = rotate(a, rx, ry, rz);
  b = rotate(b, rx, ry, rz);
  d = rotate(d, rx, ry, rz);
  float ax, ay, bx, by, dx, dy;
  project(a, &ax, &ay);
  project(b, &bx, &by);
  project(d, &dx, &dy);
  SDL_FColor col = { c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f };
  push_tri(ax, ay, bx, by, dx, dy, col);
  return 0;
}

// g3d.card(cx,cy,cz, w,h [, opts]) -- a flat rectangular card centred at
// (cx,cy,cz), w wide and h tall in its own plane (local z = 0), rotated about
// its centre by rx/ry/rz, then projected. Phase 1: a solid face with an
// optional border; the lift/tilt "feel" is driven by the scene through the
// rotation + position it passes each frame. opts:
//   color  = {r,g,b,a}  face colour (default near-white)
//   border = {r,g,b,a}  frame colour (default white); border = width in px
//   rx/ry/rz            rotation about the card centre (radians)
static int l_card(lua_State *L) {
  float cx = (float)luaL_checknumber(L, 1);
  float cy = (float)luaL_checknumber(L, 2);
  float cz = (float)luaL_checknumber(L, 3);
  float w  = (float)luaL_checknumber(L, 4);
  float h  = (float)luaL_checknumber(L, 5);
  int t = lua_istable(L, 6) ? 6 : 0;

  Uint8 face[4]; opt_rgba_key(L, t, "color",  face, 235, 235, 245, 255);
  Uint8 bord[4]; opt_rgba_key(L, t, "border", bord, 255, 255, 255, 255);
  float bw = opt_f(L, t, "border", 0);   // border is a width (number) here
  float rx = opt_f(L, t, "rx", 0), ry = opt_f(L, t, "ry", 0), rz = opt_f(L, t, "rz", 0);

  // material = "holo"|"chrome"|"glass" picks a per-pixel shaded surface; without
  // it the card is a plain flat-shaded quad (cheap, no texture).
  int mat = MAT_FLAT;
  SDL_Surface *base = NULL;
  if (t) {
    lua_getfield(L, t, "material");
    if (lua_isstring(L, -1)) mat = material_from_name(lua_tostring(L, -1));
    lua_pop(L, 1);
    // material="..." with an art image -> the material blends over the picture.
    lua_getfield(L, t, "image");
    if (lua_isstring(L, -1)) base = load_card_art(lua_tostring(L, -1));
    lua_pop(L, 1);
  }

  (void)rz;   // cards tilt by facing direction, not by a roll angle
  // No-twist tilt: rather than stacking Euler X/Y rotations (which add a roll as
  // the card swings -- it looks like it spins about the axis out of the camera),
  // point the card's normal toward the target along the SHORTEST arc. rx/ry are
  // read as the desired pitch/yaw of the normal; the card then tilts about a
  // single in-plane axis (perpendicular to the view) with zero spin.
  float tx = sinf(ry), ty = sinf(rx);                 // target tilt of the normal
  float nl = sqrtf(tx * tx + ty * ty + 1.0f);
  Vec3  nrm = { tx / nl, ty / nl, -1.0f / nl };        // target face normal
  // Shortest-arc rotation (0,0,-1) -> nrm: axis = (0,0,-1) x nrm = (ny, -nx, 0),
  // |axis| = sin(theta), cos(theta) = -nrm.z.
  float s = sqrtf(nrm.x * nrm.x + nrm.y * nrm.y), c = -nrm.z;
  Vec3 kax = (s > 1e-6f) ? (Vec3){ nrm.y / s, -nrm.x / s, 0.0f } : (Vec3){ 1, 0, 0 };

  // Project a subdivided grid of the card face. The textured path renders this
  // grid (so the picture doesn't shear when the card tilts); the flat path and
  // the reflection use the four corners pulled from it.
  float hw = w * 0.5f, hh = h * 0.5f;
  float grid[G3D_GRIDV][2];
  for (int gj = 0; gj <= G3D_GY; gj++)
    for (int gi = 0; gi <= G3D_GX; gi++) {
      float lu = (float)gi / G3D_GX, lv = (float)gj / G3D_GY;
      Vec3 lp = { (lu * 2.0f - 1.0f) * hw, (1.0f - lv * 2.0f) * hh, 0.0f };
      Vec3 r = rotate_axis(lp, kax, c, s);
      int id = gj * (G3D_GX + 1) + gi;
      project((Vec3){ r.x + cx, r.y + cy, r.z + cz }, &grid[id][0], &grid[id][1]);
    }
  int iTL = 0, iTR = G3D_GX, iBR = G3D_GRIDV - 1, iBL = G3D_GY * (G3D_GX + 1);
  float P[4][2] = {
    { grid[iTL][0], grid[iTL][1] }, { grid[iTR][0], grid[iTR][1] },
    { grid[iBR][0], grid[iBR][1] }, { grid[iBL][0], grid[iBL][1] },
  };

  float facing = -nrm.z;   // = c; how square-on the card is to the camera
  if (facing < 0.0f) facing = 0.0f;

  if (mat != MAT_FLAT) {
    // Shaded card: record a command; g3d_flush rasterises the material into a
    // texture and draws the perspective quad.
    if (g_ncards < G3D_MAX_CARDS) {
      G3DCard *cd = &g_cards[g_ncards++];
      for (int i = 0; i < 4; i++) cd->p[i] = (SDL_FPoint){ P[i][0], P[i][1] };
      memcpy(cd->grid, grid, sizeof grid);
      cd->material = mat;
      cd->rx = rx; cd->ry = ry; cd->facing = facing;
      cd->base = base;
    }
    return 0;
  }

  // Flat card: a solid shaded quad with an optional straight border.
  float k = 0.62f + 0.38f * facing;
  SDL_FColor fc = { face[0] / 255.0f * k, face[1] / 255.0f * k,
                    face[2] / 255.0f * k, face[3] / 255.0f };
  push_tri(P[0][0], P[0][1], P[1][0], P[1][1], P[2][0], P[2][1], fc);
  push_tri(P[0][0], P[0][1], P[2][0], P[2][1], P[3][0], P[3][1], fc);

  if (bw > 0.0f)
    for (int e = 0; e < 4; e++) {
      int a = e, b = (e + 1) & 3;
      push_line(P[a][0], P[a][1], P[b][0], P[b][1],
                bord[0], bord[1], bord[2], bord[3], bw);
    }
  return 0;
}

// g3d.project(x,y,z) -> sx, sy, scale -- map a world point to logical-screen
// coordinates and return the perspective scale (logical pixels per world unit at
// that depth), so a scene can hit-test a card by projecting its centre and
// sizing the hover rect by scale. Pure query; emits nothing.
static int l_project(lua_State *L) {
  Vec3 p = { (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
             (float)luaL_checknumber(L, 3) };
  float sx, sy;
  project(p, &sx, &sy);
  float depth = p.z - g_cam_z;
  if (depth < 0.001f) depth = 0.001f;
  lua_pushnumber(L, sx);
  lua_pushnumber(L, sy);
  lua_pushnumber(L, focal_len() / depth);
  return 3;
}

// g3d.camera(x, y, z) -- move the camera (it looks along +z).
static int l_camera(lua_State *L) {
  g_cam_x = (float)luaL_checknumber(L, 1);
  g_cam_y = (float)luaL_checknumber(L, 2);
  g_cam_z = (float)luaL_checknumber(L, 3);
  return 0;
}

// g3d.fov(degrees) -- set the vertical field of view (clamped to a sane range).
static int l_fov(lua_State *L) {
  float d = (float)luaL_checknumber(L, 1);
  if (d < 1.0f)   d = 1.0f;
  if (d > 179.0f) d = 179.0f;
  g_fov_deg = d;
  return 0;
}

void g3d_register(lua_State *L) {
  static const luaL_Reg fns[] = {
    { "camera", l_camera },
    { "fov",    l_fov },
    { "line",    l_line },
    { "tri",     l_tri },
    { "cube",    l_cube },
    { "card",    l_card },
    { "project", l_project },
    { NULL, NULL },
  };
  luaL_newlib(L, fns);
  lua_setglobal(L, "g3d");
}
