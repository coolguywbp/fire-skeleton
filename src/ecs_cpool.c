// ecs_cpool.c - dense component pool (sparse set)

#include "ecs_cpool.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct cpool_t {
    size_t elem_size;

    // Dense, packed arrays (index 0..count-1).
    unsigned char *data; // component data, elem_size bytes each
    hash_t *dense_ent;   // owning entity id for each dense slot
    size_t count;
    size_t capacity;

    // sparse[ent] = dense index + 1, or 0 if the entity has no component.
    size_t *sparse;
    size_t sparse_cap;
};

cpool_t* cpool_alloc(size_t initial_count, size_t elem_size)
{
    cpool_t *p = calloc(1, sizeof(cpool_t));
    if (!p) return NULL;

    if (initial_count == 0) initial_count = 16;
    p->elem_size = elem_size;
    p->data = malloc(initial_count * elem_size);
    p->dense_ent = malloc(initial_count * sizeof(hash_t));
    p->sparse = calloc(initial_count, sizeof(size_t)); // 0 = absent
    if (!p->data || !p->dense_ent || !p->sparse) {
        cpool_free(p);
        return NULL;
    }
    p->capacity = initial_count;
    p->sparse_cap = initial_count;
    return p;
}

void cpool_free(cpool_t *p)
{
    if (!p) return;
    free(p->data);
    free(p->dense_ent);
    free(p->sparse);
    free(p);
}

static bool ensure_dense(cpool_t *p)
{
    if (p->count < p->capacity) return true;

    size_t nc = p->capacity * 2;
    void *d = realloc(p->data, nc * p->elem_size);
    if (!d) return false;
    p->data = d;
    hash_t *e = realloc(p->dense_ent, nc * sizeof(hash_t));
    if (!e) return false;
    p->dense_ent = e;
    p->capacity = nc;
    return true;
}

static bool ensure_sparse(cpool_t *p, hash_t ent)
{
    if (ent < p->sparse_cap) return true;

    size_t nc = p->sparse_cap ? p->sparse_cap : 16;
    while (ent >= nc) nc *= 2;
    size_t *s = realloc(p->sparse, nc * sizeof(size_t));
    if (!s) return false;
    memset(s + p->sparse_cap, 0, (nc - p->sparse_cap) * sizeof(size_t));
    p->sparse = s;
    p->sparse_cap = nc;
    return true;
}

void* cpool_get(cpool_t *p, hash_t ent)
{
    if (ent >= p->sparse_cap) return NULL;
    size_t s = p->sparse[ent];
    if (s == 0) return NULL;
    return p->data + (s - 1) * p->elem_size;
}

void* cpool_insert(cpool_t *p, hash_t ent, const void *data)
{
    void *existing = cpool_get(p, ent);
    if (existing) {
        if (data) memcpy(existing, data, p->elem_size);
        return existing;
    }

    if (!ensure_dense(p)) return NULL;
    if (!ensure_sparse(p, ent)) return NULL;

    size_t idx = p->count++;
    p->dense_ent[idx] = ent;
    p->sparse[ent] = idx + 1;

    void *slot = p->data + idx * p->elem_size;
    if (data) memcpy(slot, data, p->elem_size);
    else memset(slot, 0, p->elem_size);
    return slot;
}

void cpool_delete(cpool_t *p, hash_t ent)
{
    if (ent >= p->sparse_cap) return;
    size_t s = p->sparse[ent];
    if (s == 0) return;

    size_t idx = s - 1;
    size_t last = p->count - 1;

    // Swap-remove: move the last element into the freed slot.
    if (idx != last) {
        memcpy(p->data + idx * p->elem_size,
               p->data + last * p->elem_size, p->elem_size);
        hash_t moved = p->dense_ent[last];
        p->dense_ent[idx] = moved;
        p->sparse[moved] = idx + 1;
    }

    p->sparse[ent] = 0;
    p->count--;
}

size_t cpool_count(cpool_t *p)
{
    return p->count;
}
