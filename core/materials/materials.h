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
// Aspect ~0.72 to match a real card (and the MTG art used as a base). Kept
// modest: the material is re-shaded per pixel on the CPU, so this trades crispness
// for frame rate. The art is pre-resampled to this size once (see g3d), so the
// per-frame shade reads it 1:1.
#define CARD_TEX_W 224
#define CARD_TEX_H 312

// Map a material name ("holo"|"chrome"|"glass") to its id; MAT_FLAT if unknown
// or NULL.
int material_from_name(const char *name);

// Fill `pixels` (ARGB8888, CARD_TEX_W x CARD_TEX_H, row stride `pitch` bytes)
// with `material`. vx/vy are the view tilt taken from the card's rotation (so
// reflections/rainbow slide as it turns), `facing` (0..1) dims an edge-on card,
// and `t` is seconds for animation. Corners are rounded (transparent).
//
// If `base` is non-NULL it is the card-art image ALREADY resampled to this
// texture's size (ARGB8888, CARD_TEX_W x CARD_TEX_H, row stride base_stride in
// Uint32s), so it's read 1:1 -- no per-pixel resampling. The material then blends
// OVER the art as a foil/reflection/glass overlay instead of painting a
// procedural surface, and no synthetic frame is drawn (the art carries its own).
void material_shade(Uint32 *pixels, int pitch, int material,
                    float vx, float vy, float facing, float t,
                    const Uint32 *base, int base_w, int base_h, int base_stride);

#endif
