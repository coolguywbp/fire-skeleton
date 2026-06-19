#pragma once
#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "game.h"

/*
    A simple adaptive stress benchmark for the ECS.

    It starts with a single entity (high FPS), keeps spawning more entities
    until the smoothed frame rate drops to the target floor (recording the
    peak), then despawns back down to a small steady-state count.
*/

typedef enum {
    BENCH_RAMP_UP = 0,   // adding entities while FPS is above the floor
    BENCH_RAMP_DOWN,     // floor hit; removing entities down to the target
    BENCH_DONE           // settled at the steady-state count
} BenchPhase;

struct Benchmark {
    bool active;
    BenchPhase phase;

    // Spawned entity ids (grown geometrically), used for ordered despawn.
    Entity *ids;
    size_t count;
    size_t capacity;

    // Highest entity count reached before dropping to the FPS floor.
    size_t peak;
    // Entity count at the last FPS sample (used to record an accurate peak).
    size_t prev_count;

    // FPS sampling window.
    double accum_time;
    int accum_frames;
    double smoothed_fps;

    // Time spent at the steady state after finishing, before auto-restarting.
    double done_time;
};

Benchmark* benchmark_new(void);
void benchmark_free(Benchmark *b);

// Reset and begin a fresh run (spawns the first entity).
void benchmark_start(struct Game *G);
// Despawn everything and go inactive (e.g. when leaving the level).
void benchmark_stop(struct Game *G);
// Call once per frame while in the level; drives spawning/despawning.
void benchmark_update(struct Game *G);

#endif
