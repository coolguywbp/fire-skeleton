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
//   g3d.camera(x, y, z)    -- move the camera (it looks along +z)
//   g3d.fov(degrees)       -- vertical field of view (framing/zoom)
//   g3d.line(x1,y1,z1, x2,y2,z2 [, {color={r,g,b,a}, width=, rx=, ry=, rz=}])
//   g3d.tri(x1,y1,z1, x2,y2,z2, x3,y3,z3 [, {color=, rx=, ry=, rz=}])
//   g3d.cube(cx,cy,cz, size       [, {color=, width=, rx=, ry=, rz=, fill=, wire=}])
//
// rx/ry/rz are rotation angles in radians applied about the primitive's own
// origin before projection. World space is right-handed with +y up; the camera
// looks along +z. Camera position and FOV persist until changed.
void g3d_register(struct lua_State *L);
void g3d_begin_frame(void);               // reset the per-frame primitive list
void g3d_flush(struct SDL_Renderer *r);   // project + draw what was emitted

#endif
