// ecs_cpool.h - dense component pool (sparse set)

#pragma once
#ifndef ECS_CPOOL_H
#define ECS_CPOOL_H

#include "ecs_hash.h"
#include <stddef.h>

/*
    A sparse-set storage for one component type.

    Component data is kept in a single contiguous "dense" array (great cache
    locality), with a sparse array mapping an entity id to its dense index.
    Lookups are O(1) plain array indexing - no hashing, no bucket walks - which
    is the hot-path cost in system updates. Removal is an O(1) swap-remove, so
    cached pointers into the data are NOT stable across deletions (the ECS
    re-fetches component pointers every update, so this is fine).
*/
typedef struct cpool_t cpool_t;

cpool_t* cpool_alloc(size_t initial_count, size_t elem_size);
void cpool_free(cpool_t *p);

// Insert (or return existing) the component for entity `ent`. If `data` is
// non-NULL it is copied in, otherwise the slot is zeroed. Returns the element.
void* cpool_insert(cpool_t *p, hash_t ent, const void *data);

// Returns a pointer to entity `ent`'s component, or NULL if absent.
void* cpool_get(cpool_t *p, hash_t ent);

// Removes entity `ent`'s component (swap-remove). No-op if absent.
void cpool_delete(cpool_t *p, hash_t ent);

// Number of live components.
size_t cpool_count(cpool_t *p);

#endif
