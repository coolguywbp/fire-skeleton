<h1 align="center">Fire Skeleton Invader</h1>

<p align="center">
  <img src="assets/images/MenuFireSkeleton.png" alt="Fire Skeleton" width="170">
</p>

<p align="center">
  <i>A hand-written 2D game engine in pure C — a custom ECS for speed, a Lua layer for gameplay — with a benchmark to prove it scales.</i>
</p>

<p align="center">
  <img src="assets/demo.gif" alt="Benchmark: tens of thousands of sprites, each bouncing and repelled by the cursor" width="480">
  <br>
  <sub>The benchmark at full tilt — tens of thousands of sprites, each independently simulated and rendered, repelled by the cursor.</sub>
</p>

---

An educational project written in **C11**, built to design and stress-test a
custom **Entity Component System (ECS)** from scratch. The engine, every data
structure it relies on, the worker pool and the logger are all hand-written,
with no third-party code beyond the windowing/UI libraries.

The hot path — simulating and rendering tens of thousands of entities — stays in
C, while **everything above it is written in Lua**: gameplay, scenes, and even
the menus. Each scene is its own script swapped in at runtime:

- **menu** (`scripts/menu.lua`) — the main menu and options screen.
- **PLAY** (`scripts/invaders.lua`) — a small but complete **Space Invaders** clone.
- **BENCHMARK** (`scripts/benchmark.lua`) — an adaptive ECS stress test.

The interface (menus, HUD, game-over screens) is drawn from Lua with an
immediate-mode **UI toolkit** that sits on top of **Clay**, a layout library.

> Status: a playable demo and a stress benchmark, both scripted in Lua. The
> focus is the architecture and how far it scales. Learning sandbox / WIP.

## Results

A release build (`-O3 -march=native -flto`) sustains tens of thousands of
independently-simulated and individually-rendered sprites at interactive frame
rates — each with its own motion, edge bouncing, and repulsion from the mouse
cursor. The exact number is hardware-dependent, so rather than quote a figure,
run **BENCHMARK**: it ramps the entity count until the frame rate hits a floor
and reports the peak your machine sustains.

- Clean under **AddressSanitizer + LeakSanitizer** — no leaks, no memory errors

## Tech stack

| Area | Technology |
|------|------------|
| Language | C11 |
| Game-logic scripting | [Lua 5.4](https://www.lua.org/) |
| Windowing / input / rendering | [SDL3](https://www.libsdl.org/) |
| Textures | SDL3_image |
| Text / fonts | SDL3_ttf ([Liberation Sans](https://github.com/liberationfonts/liberation-fonts), SIL OFL) |
| Compression | zlib |
| UI | immediate-mode Lua toolkit over [Clay](https://github.com/nicbarker/clay) (vendored as `src/clay.h`) |
| Concurrency | POSIX threads |
| Build | GNU Make + GCC (native), Emscripten (web, WIP) |
| Debugging | AddressSanitizer + LeakSanitizer |

The ECS, its containers, the worker pool and the logger are written from scratch
with no third-party dependencies.

## Architecture

A from-scratch ECS, with its design inspired by
[sturnclaw/ecs-c](https://github.com/sturnclaw/ecs-c):

- **Entities** are lightweight integer ids.
- **Components** are plain data (`Transform`, `Velocity`, `Sprite`).
- **Systems** hold update logic (`VelocitySystem`, `SpriteRenderSystem`),
  operate on entities matching an **archetype**, and declare ordering
  dependencies.
- The scheduler respects those dependencies and runs thread-safe systems across
  a worker pool, keeping renderer-touching systems on the main thread.

It is backed by purpose-built containers (`src/ecs_*.c`): hashtables, a **dense
sparse-set component pool** (contiguous data, O(1) lookups), dynamic arrays, a
memory pool, and a **barrier-based thread pool**.

The game layer is a classic `events → update → render` loop. Scenes (menu,
options, level) are Lua scripts loaded on demand; each frame the active script
emits its UI through the toolkit, Clay lays it out, and an SDL3 backend
(`src/clay_renderer.c`) draws it.

## Game logic & scripting (Lua)

Game logic lives in **Lua** (`scripts/`), embedded via a single main-thread
state (`src/script.c`). The guiding rule is **script the rules, not the inner
loop**: the heavy per-frame work — moving and rendering every entity — stays in
C systems, while Lua drives the sparse, high-level decisions (spawning, waves,
events, scoring). Lua is only ever called a handful of times per frame, never
once-per-entity, so the throughput the engine is built for is preserved. Both
the Space Invaders demo and the benchmark are *entirely* Lua scripts.

Scripts get a small C API and a few callbacks:

```lua
-- a prefab is a named template -> registers an ECS archetype under the hood
prefab "Invader" {
  Transform = { w = 48, h = 48 },
  Sprite    = { image = "skeleton" },
  Collision = {},                  -- opt in to collision detection
}

function on_start()                -- scene setup
  start(wave)                      -- run a coroutine-based "director"
end

function wave()                    -- reads top-to-bottom; wait() yields
  for i = 1, 5 do spawn_at("Invader", 100 * i, -20); wait(0.5) end
end

function on_update(dt) ... end     -- once per frame (not per entity)
function on_key(key)  ... end      -- "left"/"right"/"space"/...
function on_collision(a, b) ... end -- C spatial-hash broad-phase calls back here
```

Exposed to Lua: `prefab`, `spawn` / `spawn_at` / `spawn_many` / `destroy` /
`despawn` / `count`, `set_pos` / `get_pos`, `key_down`, `fps`, `hud`, the
coroutine helpers `start` / `wait`, and `SCREEN_W` / `SCREEN_H`. Collisions use
a uniform **spatial hash** over an opt-in `CollisionComponent`, so the
benchmark's bouncing sprites (which lack it) cost nothing.

**Hot reload:** the active script's file is watched, and on change a fresh Lua
state is built and swapped in *only if it loads cleanly* — a syntax error leaves
the running game untouched. Edit a `.lua`, save, and the scene rebuilds with no
recompile.

### UI: immediate-mode toolkit + declarative layouts

The interface is drawn from Lua. At the bottom is an **immediate-mode toolkit**
(`src/ui_lua.c`, the `ui` table — `ui.panel` / `ui.label` / `ui.button` /
`ui.text` / `ui.rect` / `ui.image`) that emits Clay elements directly, with
CSS-like options (per-side padding, `grow` / `fit` / percent sizing, borders,
colors).

On top of it sits a tiny **declarative view runtime**: a scene's structure and
styling live in a layout file as a node tree plus a stylesheet (the "HTML + CSS"),
while the scene file keeps only behavior (the "JS"). `mount(spec)` turns a
layout into a view, `view:on(id, fn)` binds a button to an action, and
`view:render()` walks the tree each frame through the `ui.*` toolkit:

```lua
-- scripts/menu_view.lua  — structure + style in one file
local styles = { item = { size = 54, height = "grow", align = "left" }, ... }
return { menu = { styles = styles, tree = {
  { tag = "list", class = "list", children = {
    { tag = "button", id = "play", class = "item", text = "PLAY" }, ... } } } } }

-- scripts/menu.lua  — behavior only
local L = require("menu_view")
mount(L.menu):on("play", function() goto_scene("play") end)
```

## Benchmark

Selecting **BENCHMARK** runs an interactive ECS stress benchmark
(`scripts/benchmark.lua`):

1. Starts with a single bouncing sprite (very high FPS).
2. Spawns more entities at a steady, frame-spread rate.
3. When the smoothed frame rate hits the 30 FPS floor, it records the **peak
   entity count** sustained.
4. Despawns back to the base count, pauses briefly, then loops.

Sprites are repelled from a circle around the mouse cursor, so the load is
interactive. The HUD shows the live object count, FPS, and benchmark phase/peak,
letting you watch in real time how many independently-simulated, individually-
rendered sprites the architecture sustains.

## Why it's fast

The whole design is **data-oriented**: keep the hot per-frame loop touching
tightly packed memory and doing as little as possible per entity.

- **Dense sparse-set component storage.** Each component type lives in one
  contiguous, packed array; a sparse index maps an entity id straight to its
  slot. Access is plain array indexing — **O(1), no hashing, no pointer
  chasing** — so the CPU streams component data with great cache locality. This
  is the single biggest reason it scales.
- **Components are plain inline data** (`x, y, w, h` — not pointers to heap), so
  a whole array of them is one contiguous block with zero indirection.
- **Systems resolve their component pointers once, at registration.** The
  per-entity update is then a couple of array lookups and some arithmetic — no
  per-entity hash, no allocation.
- **The hot path stays in C; Lua only steers.** Game logic runs in Lua a handful
  of times per frame (spawning, events, scoring), never once-per-entity, so
  scripting adds essentially nothing to the cost of simulating tens of thousands
  of entities.
  Bulk spawning skips component writes the prefab doesn't set.
- **Opt-in collision.** Only entities with a `CollisionComponent` enter the
  spatial hash, so the benchmark's pure-load sprites pay nothing for collision.
- **O(1) churn, no hot-path allocation.** Deletes are swap-removes; the update
  loop allocates nothing per frame.
- **A barrier-based worker pool** can fan thread-safe systems across cores,
  while renderer-touching systems stay on the main thread.

Every decision was driven by the benchmark, not intuition — **measure before
optimizing**: A/B toggling rendering and the ECS update showed the cost was
*how component data was reached*, not the math or the draw calls, which is what
pointed straight at the storage layout.

### What could be faster still

- **Batch the rendering.** Each sprite is currently its own `SDL_RenderTexture`
  call; collapsing them into a single `SDL_RenderGeometry` (one big textured
  quad buffer) would cut draw-call overhead and is likely the next ceiling at
  high counts. *(An early attempt black-screened on this GPU — worth revisiting
  carefully.)*
- **SoA layout + SIMD.** Splitting `x/y/vx/vy` into separate arrays and
  vectorizing the motion update (SSE/AVX) could process 4–8 entities per
  instruction.
- **Spatial grid for cursor repulsion.** Right now every sprite tests its
  distance to the cursor each frame; a grid would let only nearby sprites pay.
- **Off-screen culling** so sprites outside the window skip rendering.
- **Drop redundant per-frame writes** (e.g. recomputing sprite aspect/width
  every frame) and smooth out the component pool's realloc growth spikes.
- **Lean harder on the worker pool / GPU.** Motion is embarrassingly parallel;
  once batching shifts the bottleneck back onto simulation, splitting it across
  more cores — or moving it to the GPU — becomes worthwhile.

## Building & running

Two builds from the same sources: native Linux/GCC, and an experimental
WebAssembly build via Emscripten.

### Native (Linux + GCC)

Requires `gcc`, `make`, `pkg-config` and dev packages for SDL3, SDL3_image,
SDL3_ttf, zlib and Lua 5.4.

```sh
make            # optimized release build -> ./game
make debug      # AddressSanitizer + LeakSanitizer build
make run        # build and run
make clean
```

**Controls:** the mouse navigates the menu. In **PLAY**: **← / →** to move,
**Space** to shoot. In **BENCHMARK**: move the mouse to repel sprites. **Q /
Esc** returns to the menu; **E** toggles the Clay debug overlay (debug builds).

### WebAssembly (Emscripten) — experimental / WIP

> ⚠️ **Unfinished.** The wasm target *compiles and links*, but the web build is
> not yet verified to run correctly in a browser — more work is needed (canvas/
> WebGL setup, input mapping, asset paths, and rethinking the benchmark, since
> browser frames are capped at the display refresh rate). Treat it as a starting
> point, not a finished feature.

Requires the [Emscripten SDK](https://emscripten.org/) (`emcc`) and an
Emscripten-built SDL3 stack (SDL3 + SDL3_image + SDL3_ttf compiled for wasm).
Place it in `vendor/sdl3-wasm/` (prebuilt libs are not committed — large and
tied to specific versions), then:

```sh
make web                                 # uses vendor/sdl3-wasm by default
make web SDL3_WASM=/path/to/sdl3-stack   # or point elsewhere
python3 -m http.server --directory web   # serve web/game.html
```

The web build runs single-threaded (browser threads need SharedArrayBuffer /
COOP-COEP), is driven by `requestAnimationFrame`, and bundles assets into
`game.data`.

## By the numbers

- ~**7,400** lines of hand-written C across **71 files**, plus ~**290** lines of
  Lua game logic
- ~**40%** of the C is the engine itself — the ECS plus its data structures
  (~2,500 lines: core, hashtables/arrays, the dense component pool, the thread
  pool)
- The Lua bridge + spatial-hash collision add ~**940** lines of C
- The rest: rendering, UI integration, and infrastructure
- Third-party `clay.h` (~4,500 lines) is vendored and not counted above

## Project layout

```
src/
  game.c / main.c           game loop, lifecycle, scenes
  init_*.c                  SDL, Clay and ECS initialization
  ecs*.c                    ECS API, manager, storage, containers, worker pool
  components.c / systems.c  game components and systems
  script.c                  Lua scripting layer (API, prefabs, hot reload)
  collision.c               spatial-hash collision broad-phase
  ui.c / clay_renderer.c    Clay UI and its SDL3 backend
  logger.c                  leveled logger
  clay.h                    vendored Clay library
scripts/
  invaders.lua              Space Invaders demo (PLAY)
  benchmark.lua             adaptive ECS stress benchmark (BENCHMARK)
```

## Acknowledgements

- [**sturnclaw/ecs-c**](https://github.com/sturnclaw/ecs-c) — inspiration and
  reference for the ECS architecture.
- [**nicbarker/clay**](https://github.com/nicbarker/clay) — the immediate-mode
  UI layout library used for menus and the HUD (vendored as `src/clay.h`).
- [**Lua**](https://www.lua.org/) — the embedded scripting language for game
  logic.
- [**Liberation Sans**](https://github.com/liberationfonts/liberation-fonts) — a
  free (SIL OFL) Helvetica/Arial-metric font with Cyrillic coverage, bundled
  under `assets/fonts/` (see its `LICENSE`).

## License

Released under the [MIT License](LICENSE) — a permissive license compatible with
the dependencies (Clay is zlib-licensed; SDL3 is zlib-licensed). `src/clay.h`
remains under its own license.
