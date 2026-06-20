#include "collision.h"

#include "game.h"
#include "components.h"
#include "script.h"
#include "logger.h"

#include "ecs_manager.h"   // Manager_GetComponentType, ComponentType
#include "ecs_cpool.h"     // cpool_count / cpool_entity_at / cpool_get
#include "ecs_entity.h"    // ECS_EntityExists

#include <stdlib.h>
#include <math.h>
#include <stdint.h>

// Grid cell size: a bit larger than a typical sprite so most entities touch
// only one or a few cells.
#define CELL_SIZE 96.0f

typedef struct {
  Entity e;
  float x, y, w, h;
} Collider;

// One insertion of a collider into one grid cell, chained per bucket.
typedef struct {
  int idx;   // index into the colliders array
  int next;  // next node in the same bucket, or -1
} GNode;

static int cell_coord(float v) { return (int)floorf(v / CELL_SIZE); }

// Hash a cell coordinate to a bucket. The two large primes scatter cells; hash
// collisions only cost a few extra AABB tests (the narrow phase rejects them),
// never wrong results.
static uint32_t cell_hash(int cx, int cy) {
  return (uint32_t)cx * 73856093u ^ (uint32_t)cy * 19349663u;
}

static bool aabb_overlap(const Collider *a, const Collider *b) {
  return a->x < b->x + b->w && a->x + a->w > b->x &&
         a->y < b->y + b->h && a->y + a->h > b->y;
}

void collision_update(struct Game *G) {
  ECS *ecs = G->ecs;

  ComponentType *coll_t = Manager_GetComponentType(ecs, COMPONENT_ID(CollisionComponent));
  ComponentType *tr_t   = Manager_GetComponentType(ecs, COMPONENT_ID(TransformComponent));
  if (!coll_t || !tr_t) return;

  size_t n = cpool_count(coll_t->components);
  if (n < 2) return;

  // 1) Gather colliders: each collidable entity's AABB from its Transform.
  Collider *cs = malloc(n * sizeof(Collider));
  if (!cs) return;
  size_t m = 0;
  for (size_t i = 0; i < n; i++) {
    Entity e = cpool_entity_at(coll_t->components, i);
    TransformComponent *t = cpool_get(tr_t->components, e);
    if (!t) continue; // collidable but no Transform: nothing to test
    cs[m].e = e;
    cs[m].x = t->x; cs[m].y = t->y; cs[m].w = t->w; cs[m].h = t->h;
    m++;
  }
  if (m < 2) { free(cs); return; }

  // 2) Build the spatial hash. Buckets are a power of two for cheap masking.
  size_t bcount = 16;
  while (bcount < m * 4) bcount <<= 1;

  int *heads = malloc(bcount * sizeof(int));
  size_t node_cap = m * 4 + 16;
  GNode *nodes = malloc(node_cap * sizeof(GNode));
  if (!heads || !nodes) { free(heads); free(nodes); free(cs); return; }
  for (size_t i = 0; i < bcount; i++) heads[i] = -1;

  size_t nn = 0;
  for (size_t i = 0; i < m; i++) {
    int x0 = cell_coord(cs[i].x), x1 = cell_coord(cs[i].x + cs[i].w);
    int y0 = cell_coord(cs[i].y), y1 = cell_coord(cs[i].y + cs[i].h);
    for (int cx = x0; cx <= x1; cx++) {
      for (int cy = y0; cy <= y1; cy++) {
        if (nn == node_cap) {
          node_cap *= 2;
          GNode *gp = realloc(nodes, node_cap * sizeof(GNode));
          if (!gp) goto detect; // degrade gracefully: test what we inserted
          nodes = gp;
        }
        uint32_t b = cell_hash(cx, cy) & (bcount - 1);
        nodes[nn].idx = (int)i;
        nodes[nn].next = heads[b];
        heads[b] = (int)nn;
        nn++;
      }
    }
  }

detect:;
  // 3) Narrow phase. `stamp[j] == i` means pair (i, j) was already tested while
  // scanning i's cells (a pair can share several cells); this dedupes it.
  int *stamp = malloc(m * sizeof(int));
  if (!stamp) { free(nodes); free(heads); free(cs); return; }
  for (size_t i = 0; i < m; i++) stamp[i] = -1;

  for (size_t i = 0; i < m; i++) {
    int x0 = cell_coord(cs[i].x), x1 = cell_coord(cs[i].x + cs[i].w);
    int y0 = cell_coord(cs[i].y), y1 = cell_coord(cs[i].y + cs[i].h);
    for (int cx = x0; cx <= x1; cx++) {
      for (int cy = y0; cy <= y1; cy++) {
        uint32_t b = cell_hash(cx, cy) & (bcount - 1);
        for (int ni = heads[b]; ni != -1; ni = nodes[ni].next) {
          size_t j = (size_t)nodes[ni].idx;
          if (j <= i) continue;            // ordered pairs, no self-test
          if (stamp[j] == (int)i) continue; // already tested this pair
          stamp[j] = (int)i;
          if (aabb_overlap(&cs[i], &cs[j])) {
            // A pair handled earlier this frame may have destroyed one of these
            // entities; only dispatch if both are still alive.
            if (ECS_EntityExists(ecs, cs[i].e) && ECS_EntityExists(ecs, cs[j].e))
              script_on_collision(G, cs[i].e, cs[j].e);
          }
        }
      }
    }
  }

  free(stamp);
  free(nodes);
  free(heads);
  free(cs);
}
