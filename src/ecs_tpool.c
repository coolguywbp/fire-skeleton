// ecs_tpool.c - a minimal barrier-based "parallel for" thread pool.

#define _POSIX_C_SOURCE 200112L

#include "ecs_tpool.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

struct tpool_t {
    size_t n; // worker threads
    pthread_t *threads;

    pthread_mutex_t mtx;
    pthread_cond_t work_cv; // wake workers for a new job
    pthread_cond_t done_cv; // notify caller a worker finished

    // Current job.
    tpool_fn fn;
    void *ctx;
    size_t *starts;
    size_t *ends;

    // Generation counter: bumped per job. A worker proceeds whenever its last
    // seen generation differs from the current one, which avoids lost wakeups.
    unsigned long generation;
    unsigned long *seen;
    size_t completed;

    bool stop;
};

typedef struct {
    tpool_t *p;
    size_t idx;
} worker_arg;

static void* worker_main(void *varg)
{
    worker_arg *wa = varg;
    tpool_t *p = wa->p;
    size_t i = wa->idx;
    free(wa);

    pthread_mutex_lock(&p->mtx);
    for (;;) {
        while (!p->stop && p->seen[i] == p->generation)
            pthread_cond_wait(&p->work_cv, &p->mtx);
        if (p->stop) break;

        p->seen[i] = p->generation;
        tpool_fn fn = p->fn;
        void *ctx = p->ctx;
        size_t s = p->starts[i];
        size_t e = p->ends[i];
        pthread_mutex_unlock(&p->mtx);

        if (fn && e > s) fn(ctx, s, e);

        pthread_mutex_lock(&p->mtx);
        p->completed++;
        pthread_cond_signal(&p->done_cv);
    }
    pthread_mutex_unlock(&p->mtx);
    return NULL;
}

tpool_t* tpool_new(size_t nthreads)
{
    tpool_t *p = calloc(1, sizeof(tpool_t));
    if (!p) return NULL;

    pthread_mutex_init(&p->mtx, NULL);
    pthread_cond_init(&p->work_cv, NULL);
    pthread_cond_init(&p->done_cv, NULL);

    if (nthreads == 0) return p; // valid pool, no workers

    p->threads = calloc(nthreads, sizeof(pthread_t));
    p->starts = calloc(nthreads, sizeof(size_t));
    p->ends = calloc(nthreads, sizeof(size_t));
    p->seen = calloc(nthreads, sizeof(unsigned long));
    if (!p->threads || !p->starts || !p->ends || !p->seen) {
        tpool_free(p);
        return NULL;
    }

    for (size_t i = 0; i < nthreads; i++) {
        worker_arg *wa = malloc(sizeof(worker_arg));
        if (!wa) break;
        wa->p = p;
        wa->idx = i;
        if (pthread_create(&p->threads[i], NULL, worker_main, wa) != 0) {
            free(wa);
            break; // run with however many we managed to start
        }
        p->n = i + 1;
    }
    return p;
}

void tpool_free(tpool_t *p)
{
    if (!p) return;

    pthread_mutex_lock(&p->mtx);
    p->stop = true;
    p->generation++;
    pthread_cond_broadcast(&p->work_cv);
    pthread_mutex_unlock(&p->mtx);

    for (size_t i = 0; i < p->n; i++)
        pthread_join(p->threads[i], NULL);

    pthread_mutex_destroy(&p->mtx);
    pthread_cond_destroy(&p->work_cv);
    pthread_cond_destroy(&p->done_cv);
    free(p->threads);
    free(p->starts);
    free(p->ends);
    free(p->seen);
    free(p);
}

size_t tpool_threads(tpool_t *p)
{
    return p ? p->n : 0;
}

void tpool_run(tpool_t *p, tpool_fn fn, void *ctx, size_t start, size_t end)
{
    if (end <= start) return;

    if (!p || p->n == 0) {
        fn(ctx, start, end);
        return;
    }

    const size_t total = end - start;
    const size_t chunks = p->n + 1; // workers + the calling thread
    const size_t per = total / chunks;

    pthread_mutex_lock(&p->mtx);
    p->fn = fn;
    p->ctx = ctx;
    size_t cur = start;
    for (size_t i = 0; i < p->n; i++) {
        p->starts[i] = cur;
        cur += per;
        p->ends[i] = cur;
    }
    size_t caller_start = cur;
    size_t caller_end = end; // caller mops up the remainder
    p->completed = 0;
    p->generation++;
    pthread_cond_broadcast(&p->work_cv);
    pthread_mutex_unlock(&p->mtx);

    if (caller_end > caller_start) fn(ctx, caller_start, caller_end);

    pthread_mutex_lock(&p->mtx);
    while (p->completed < p->n)
        pthread_cond_wait(&p->done_cv, &p->mtx);
    pthread_mutex_unlock(&p->mtx);
}

size_t tpool_hw_concurrency(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (size_t)n : 1;
}
