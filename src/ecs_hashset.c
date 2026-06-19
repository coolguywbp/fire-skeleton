// hashset.c - A hash-based set implementation.

#include "ecs_hashset.h"
#include "ecs_mempool.h"
#include "ecs_macros.h"
#include "logger.h"

#include <stdlib.h>

#define GET_IDX(hs, hash) ((hash) % (hs)->size)
#define GET_BUCKET(hs, hash) hs->buckets[GET_IDX(hs, hash)]

typedef struct bucket_t bucket_t;

struct hashset_t {
    hash_t size;
    hash_t count;
    bucket_t **buckets;
    mempool_t *storage;
};

struct bucket_t {
    hash_t hash;
    bucket_t *next;
};

hashset_t* hs_alloc(size_t initial_size)
{
    hashset_t *hs = malloc(sizeof(hashset_t));
    hs->buckets = calloc(initial_size, sizeof(bucket_t *));
    hs->storage = mp_init(initial_size, sizeof(bucket_t));
    hs->size = initial_size;
    hs->count = 0;
    return hs;
}

void hs_free(hashset_t *hs)
{
    mp_destroy(hs->storage);
    free(hs->buckets);
    free(hs);
}

void hs_resize(hashset_t *hs)
{
    size_t oldsize = hs->size;
    size_t newsize = hs->size * 2;

    LOG_DEBUG("Resizing a hashset from %u to %u", oldsize, newsize);
    // Allocate new buckets array
    bucket_t **new_buckets = calloc(newsize, sizeof(bucket_t *));

    // Rehash all existing elements
    for (size_t idx = 0; idx < oldsize; idx++) {
        bucket_t *bk = hs->buckets[idx];

        while (bk) {
            bucket_t *next = bk->next;
            // Calculate index using the NEW size
            size_t n_idx = bk->hash % newsize;

            // Insert at head of new bucket chain
            bk->next = new_buckets[n_idx];
            new_buckets[n_idx] = bk;

            bk = next;
        }
    }

    // Free old buckets array and replace with new one
    free(hs->buckets);
    hs->buckets = new_buckets;
    hs->size = newsize;
}


void hs_set(hashset_t *hs, hash_t hash)
{
    LOG_DEBUG("Setting a new value to a hashset (count %d | size %u)", hs->count, hs->size);
    if (hs->count > (float)hs->size * 0.7) hs_resize(hs);

    size_t idx = GET_IDX(hs, hash);
    bucket_t *bk = hs->buckets[idx];
    bucket_t **prev = &hs->buckets[idx];

    while (bk) {
        if (bk->hash == hash) return;
        prev = &bk->next;
        bk = bk->next;
    }

    bucket_t *next = mp_alloc(hs->storage);
    next->hash = hash;
    next->next = NULL;
    *prev = next;
    hs->count++;
}

bool hs_get(hashset_t *hs, hash_t hash)
{
    bucket_t *bk = GET_BUCKET(hs, hash);
    // Loop through until we encounter NULL or the hash.
    while (bk && bk->hash != hash) bk = bk->next;

    return bk != NULL;
}

hash_t hs_next(hashset_t *hs, hash_t hash)
{
    if (!hs->count) return 0;

    size_t idx = GET_IDX(hs, hash);
    bucket_t *bk = hs->buckets[idx];
    
    // Walk the current bucket until we find the start hash or the end
    while (bk) {
        if (bk->next && bk->next->hash == hash) return bk->hash;
        bk = bk->next;
	}

    // Find the next filled bucket.
    while (idx < hs->size && !bk) bk = hs->buckets[++idx];
    if (!bk) return 0;

    // Get the last entry in the bucket.
    while(bk->next) bk = bk->next;
    return bk->hash;
}

void hs_test(hashset_t *hs, const char **values){
  LOG_DEBUG("Hashset visualization");
  // printf("\nHashset visualization:\n");
  for (int i = 0; i < (int)hs->size; i++) {
      printf("Bucket %d: ", i);
      bucket_t* bk = hs->buckets[i];
      while (bk != NULL) {
          printf("%x -> ", bk->hash);
          bk = bk->next;
      }
      printf("NULL\n");
  }
  
  LOG_DEBUG("Checking if all values are present:");
  int found_count = 0;
  int i = 0;
  while (values[i]){
    bool found = hs_get(hs, hash_string(values[i]));
    printf("%s: %s\n", values[i], found ? "true" : "false");
    if (found) found_count++;
    i++;
  }
}

void hs_clear(hashset_t *hs, hash_t hash)
{
    bucket_t *bk = GET_BUCKET(hs, hash);
    bucket_t **bk2 = &GET_BUCKET(hs, hash);

    // Remove the hash from the bucket and close the gaps.
    while (bk) {
        if (bk->hash == hash) {
            *bk2 = bk->next;
            mp_free(hs->storage, bk);
            hs->count--;
            return;
        }

        if (!bk->next) return;
        bk2 = &bk->next;
        bk = bk->next;
    }
}
