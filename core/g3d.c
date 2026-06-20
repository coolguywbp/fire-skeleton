#include "g3d.h"
#include "game.h"   // g_logical_w/h + SDL types (via main.h)

#include <lua.h>
#include <lauxlib.h>
#include <math.h>

// Perspective camera. Primitives live near the origin; we push them CAM_DIST
// down +z so the perspective divide is well-defined, and scale by a focal
// length tied to the logical height so the result looks the same at any
// resolution (the renderer's logical presentation handles the final letterbox).
#define CAM_DIST 4.0f
#define FOCAL_K  0.9f

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

void g3d_begin_frame(void) { g_nlines = 0; g_ntris = 0; }

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

// World point -> 2D logical-screen coordinates (perspective projection).
static void project(Vec3 p, float *sx, float *sy) {
  float z = p.z + CAM_DIST;
  if (z < 0.001f) z = 0.001f;             // never divide through the camera plane
  float focal = FOCAL_K * (float)g_logical_h;
  *sx = (float)g_logical_w * 0.5f + (p.x / z) * focal;
  *sy = (float)g_logical_h * 0.5f - (p.y / z) * focal;   // +y is up
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

// opts.color = {r,g,b,a} (0-255); defaults to opaque white.
static void opt_rgba(lua_State *L, int t, Uint8 out[4]) {
  out[0] = out[1] = out[2] = out[3] = 255;
  if (!t) return;
  lua_getfield(L, t, "color");
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
      depth[f] = zc * 0.25f + cz;             // camera-space depth (CAM_DIST is constant)
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

void g3d_register(lua_State *L) {
  static const luaL_Reg fns[] = {
    { "line", l_line },
    { "tri",  l_tri },
    { "cube", l_cube },
    { NULL, NULL },
  };
  luaL_newlib(L, fns);
  lua_setglobal(L, "g3d");
}
