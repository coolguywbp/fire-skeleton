#pragma once
#ifndef G3D_H
#define G3D_H

struct lua_State;
struct SDL_Renderer;

// Software 3D primitives. All the 3D maths (rotation, perspective projection)
// and rasterisation live here in C; Lua only describes primitives through a
// `g3d` table. Immediate-mode: a scene emits primitives from on_ui(), and
// g3d_flush() projects and draws them (after the sprite pass, under the UI).
//
//   g3d.line(x1,y1,z1, x2,y2,z2 [, {color={r,g,b,a}, width=, rx=, ry=, rz=}])
//   g3d.cube(cx,cy,cz, size       [, {color={r,g,b,a}, width=, rx=, ry=, rz=}])
//
// rx/ry/rz are rotation angles in radians applied about the primitive's own
// origin before projection. World space is right-handed with +y up; the camera
// sits on +z looking toward the origin.
void g3d_register(struct lua_State *L);
void g3d_begin_frame(void);               // reset the per-frame primitive list
void g3d_flush(struct SDL_Renderer *r);   // project + draw what was emitted

#endif
