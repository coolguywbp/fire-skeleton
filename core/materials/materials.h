#pragma once
#ifndef MATERIALS_H
#define MATERIALS_H

#include <SDL3/SDL_stdinc.h>   // Uint32

// Card materials: per-pixel software "fragment shaders" for the cards demo.
// Each fills a card-sized ARGB8888 texture with a look (holographic, chrome,
// glass); g3d owns the geometry/projection and just hands these the surface to
// paint. Kept apart from g3d so new materials are a self-contained edit here.
enum { MAT_FLAT = 0, MAT_HOLO, MAT_CHROME, MAT_GLASS };

// Texture resolution the shaders paint into (the quad scales it on screen).
#define CARD_TEX_W 176
#define CARD_TEX_H 254

// Map a material name ("holo"|"chrome"|"glass") to its id; MAT_FLAT if unknown
// or NULL.
int material_from_name(const char *name);

// Fill `pixels` (ARGB8888, CARD_TEX_W x CARD_TEX_H, row stride `pitch` bytes)
// with `material`. vx/vy are the view tilt taken from the card's rotation (so
// reflections/rainbow slide as it turns), `facing` (0..1) dims an edge-on card,
// and `t` is seconds for animation. Corners are rounded (transparent) and a thin
// frame is baked just inside the edge.
void material_shade(Uint32 *pixels, int pitch, int material,
                    float vx, float vy, float facing, float t);

#endif
