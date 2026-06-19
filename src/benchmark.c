#include "benchmark.h"
#include "archetypes.h"
#include "ecs_entity.h"
#include "logger.h"

#include <stdlib.h>

// FPS floor we ramp up to.
#define BENCH_MIN_FPS 30.0
// Steady-state entity count to settle at after the peak.
#define BENCH_TARGET 1
// How long to average FPS before making a spawn/despawn decision. Long enough
// to smooth out per-frame jitter, short enough to feel responsive.
#define BENCH_SAMPLE_SEC 0.4
// Entities added/removed per second, spread evenly across frames. Spreading the
// work avoids a big single-frame spawn burst that would otherwise spike the
// frame time and make the benchmark measure spawn cost instead of sim cost.
// Kept moderate so the count climbs gradually and is easy to watch.
#define BENCH_SPAWN_RATE 1500.0
// Pause at the steady state before automatically restarting the run (seconds).
#define BENCH_RESTART_DELAY 1.5

Benchmark* benchmark_new(void)
{
    Benchmark *b = calloc(1, sizeof(Benchmark));
    return b;
}

void benchmark_free(Benchmark *b)
{
    if (!b) return;
    free(b->ids);
    free(b);
}

static void bench_push(Benchmark *b, Entity e)
{
    if (b->count == b->capacity) {
        size_t nc = b->capacity ? b->capacity * 2 : 64;
        Entity *p = realloc(b->ids, nc * sizeof(Entity));
        if (!p) return;
        b->ids = p;
        b->capacity = nc;
    }
    b->ids[b->count++] = e;
}

static void bench_spawn(struct Game *G, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        Entity e = ECS_EntityNew(G->ecs, G->archetypes[TEST_BOUNCING_SPRITE_ARCHETYPE]);
        bench_push(G->bench, e);
    }
}

static void bench_despawn(struct Game *G, size_t n)
{
    Benchmark *b = G->bench;
    for (size_t i = 0; i < n && b->count > 0; i++) {
        Entity e = b->ids[--b->count];
        ECS_EntityDelete(G->ecs, e);
    }
}

void benchmark_start(struct Game *G)
{
    Benchmark *b = G->bench;

    bench_despawn(G, b->count);
    b->phase = BENCH_RAMP_UP;
    b->peak = 0;
    b->prev_count = 0;
    b->accum_time = 0;
    b->accum_frames = 0;
    b->smoothed_fps = 0;
    b->done_time = 0;
    b->active = true;

    bench_spawn(G, 1); // start with a single object
    LOG_INFO("Benchmark started (ramp up to %.0f fps floor)", BENCH_MIN_FPS);
}

void benchmark_stop(struct Game *G)
{
    Benchmark *b = G->bench;
    if (!b->active) return;

    bench_despawn(G, b->count);
    b->active = false;
}

void benchmark_update(struct Game *G)
{
    Benchmark *b = G->bench;
    if (!b->active) return;

    // Finished: hold at the steady state briefly, then loop the whole run.
    if (b->phase == BENCH_DONE) {
        b->done_time += (double)G->dtime;
        if (b->done_time >= BENCH_RESTART_DELAY) {
            LOG_INFO("Benchmark restarting...");
            benchmark_start(G);
        }
        return;
    }

    // Spread spawning/despawning evenly across frames at a constant rate, so a
    // single frame never does a big burst. This keeps the per-frame cost flat
    // and makes the FPS reflect steady-state simulation, not spawn cost.
    size_t rate = (size_t)(BENCH_SPAWN_RATE * (double)G->dtime) + 1;

    if (b->phase == BENCH_RAMP_UP) {
        bench_spawn(G, rate);
    } else if (b->phase == BENCH_RAMP_DOWN && b->count > BENCH_TARGET) {
        size_t togo = b->count - BENCH_TARGET;
        bench_despawn(G, rate < togo ? rate : togo);
    }

    // Build up a sampling window for a stable FPS reading, then act on it.
    b->accum_time += G->dtime;
    b->accum_frames++;
    if (b->accum_time < BENCH_SAMPLE_SEC) return;

    b->smoothed_fps = b->accum_frames / b->accum_time;
    b->accum_time = 0;
    b->accum_frames = 0;

    switch (b->phase) {
    case BENCH_RAMP_UP:
        if (b->smoothed_fps <= BENCH_MIN_FPS) {
            // prev_count is the count at the previous sample, which still held
            // the floor; report it as the peak (avoids within-window overshoot).
            b->peak = b->prev_count ? b->prev_count : b->count;
            b->phase = BENCH_RAMP_DOWN;
            LOG_INFO("Benchmark peak: %zu entities sustained >= %.0f fps",
                     b->peak, BENCH_MIN_FPS);
        } else {
            b->prev_count = b->count;
        }
        break;

    case BENCH_RAMP_DOWN:
        if (b->count <= BENCH_TARGET) {
            b->phase = BENCH_DONE;
            LOG_INFO("Benchmark done. Steady state: %zu entities (~%.0f fps)",
                     b->count, b->smoothed_fps);
        }
        break;

    case BENCH_DONE:
    default:
        break;
    }
}
