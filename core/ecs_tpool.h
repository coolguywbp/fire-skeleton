// ecs_tpool.h - a minimal barrier-based "parallel for" thread pool.

#pragma once
#ifndef ECS_TPOOL_H
#define ECS_TPOOL_H

#include <stddef.h>

/*
    A small persistent worker pool for data-parallel work. tpool_run() splits a
    [start, end) index range into chunks, runs them across the worker threads
    AND the calling thread, and blocks until all chunks finish (a barrier). The
    callback must therefore be safe to run concurrently on disjoint sub-ranges.
*/
typedef struct tpool_t tpool_t;

// Callback invoked per chunk with its half-open sub-range [start, end).
typedef void (*tpool_fn)(void *ctx, size_t start, size_t end);

// Create a pool with `nthreads` worker threads (0 = run everything on the
// caller). Workers are spawned once and reused.
tpool_t* tpool_new(size_t nthreads);
void tpool_free(tpool_t *p);

// Number of worker threads actually running.
size_t tpool_threads(tpool_t *p);

// Run fn over [start, end) in parallel and wait for completion.
void tpool_run(tpool_t *p, tpool_fn fn, void *ctx, size_t start, size_t end);

// Hardware concurrency (online CPUs), or 1 if it can't be determined.
size_t tpool_hw_concurrency(void);

#endif
