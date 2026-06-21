#include "g3d.h"
#include "game.h"               // g_logical_w/h + SDL types (via main.h)
#include "materials/materials.h"  // card materials (per-pixel shaders)

#include <lua.h>
#include <lauxlib.h>
#include <math.h>

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
typedef struct {
  SDL_FPoint p[4];     // projected corners: TL, TR, BR, BL
  int   material;
  float rx, ry;        // card rotation -> view-dependent shading
  float facing;        // 0..1, how square-on to the camera (edge-on dims)
} G3DCard;
static G3DCard      g_cards[G3D_MAX_CARDS];
static int          g_ncards;
static SDL_Texture *g_card_pool[G3D_MAX_CARDS];   // lazily created, one per slot

void g3d_begin_frame(void) { g_nlines = 0; g_ntris = 0; g_ncards = 0; }

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
  for (int i = 0; i < g_ncards; i++) {
    G3DCard *c = &g_cards[i];
    SDL_Texture *tex = card_texture(renderer, i);
    if (!tex) continue;
    void *px; int pitch;
    if (SDL_LockTexture(tex, NULL, &px, &pitch)) {
      // vx/vy are the view tilt: the card's own ry/rx.
      material_shade((Uint32 *)px, pitch, c->material, c->ry, c->rx, c->facing, t);
      SDL_UnlockTexture(tex);
    }
    int idx[6] = { 0, 1, 2, 0, 2, 3 };

    // Faded reflection below the card: mirror the top corners across the bottom
    // edge (screen-space, good enough for the mild tilt), draw the same texture
    // flipped, with vertex alpha fading from the card's base to transparent.
    // Reads as depth on the black backdrop where a dark drop-shadow could not.
    SDL_FPoint r0 = { 2 * c->p[3].x - c->p[0].x, 2 * c->p[3].y - c->p[0].y };
    SDL_FPoint r1 = { 2 * c->p[2].x - c->p[1].x, 2 * c->p[2].y - c->p[1].y };
    SDL_FColor near = { 1, 1, 1, 0.28f }, far = { 1, 1, 1, 0.0f };
    SDL_Vertex rv[4] = {
      { c->p[3], near, { 0, 1 } }, { c->p[2], near, { 1, 1 } },
      { r1,      far,  { 1, 0 } }, { r0,      far,  { 0, 0 } },
    };
    SDL_RenderGeometry(renderer, tex, rv, 4, idx, 6);

    // The card itself, on top.
    SDL_FColor w = { 1, 1, 1, 1 };
    SDL_Vertex v[4] = {
      { c->p[0], w, { 0, 0 } }, { c->p[1], w, { 1, 0 } },
      { c->p[2], w, { 1, 1 } }, { c->p[3], w, { 0, 1 } },
    };
    SDL_RenderGeometry(renderer, tex, v, 4, idx, 6);
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
  if (t) {
    lua_getfield(L, t, "material");
    if (lua_isstring(L, -1)) mat = material_from_name(lua_tostring(L, -1));
    lua_pop(L, 1);
  }

  // Local corners in the card plane, wound CCW: TL, TR, BR, BL.
  float hw = w * 0.5f, hh = h * 0.5f;
  static const float CS[4][2] = { {-1, 1}, {1, 1}, {1, -1}, {-1, -1} };
  float P[4][2];
  Vec3  R[4];
  for (int i = 0; i < 4; i++) {
    Vec3 v = { CS[i][0] * hw, CS[i][1] * hh, 0.0f };
    R[i] = rotate(v, rx, ry, rz);
    Vec3 world = { R[i].x + cx, R[i].y + cy, R[i].z + cz };
    project(world, &P[i][0], &P[i][1]);
  }

  // Facing: how square-on the card is to the camera. Its local normal is
  // (0,0,-1) (toward the camera at -z); facing = -n.z after rotation.
  Vec3 n = rotate((Vec3){ 0, 0, -1 }, rx, ry, rz);
  float facing = -n.z;
  if (facing < 0.0f) facing = 0.0f;

  if (mat != MAT_FLAT) {
    // Shaded card: record a command; g3d_flush rasterises the material into a
    // texture and draws the perspective quad.
    if (g_ncards < G3D_MAX_CARDS) {
      G3DCard *cd = &g_cards[g_ncards++];
      for (int i = 0; i < 4; i++) cd->p[i] = (SDL_FPoint){ P[i][0], P[i][1] };
      cd->material = mat;
      cd->rx = rx; cd->ry = ry; cd->facing = facing;
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
