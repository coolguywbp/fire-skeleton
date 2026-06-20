<h1 align="center">Fire Skeleton</h1>

<p align="center">
  <img src="assets/images/MenuFireSkeleton.png" alt="Fire Skeleton — 2D game engine logo" width="150">
</p>

<p align="center">
  <b>A fast, minimal 2D game engine in pure C.</b><br>
  <i>A cache-friendly, multithreaded ECS · Lua scripting · a tiny dependency footprint —
  running natively on Linux &amp; Windows and in the browser via WebAssembly.</i>
</p>

<p align="center">
  <a href="https://coolguywbp.github.io/fire-skeleton/"><b>▶ Play the live demo in your browser</b></a>
  &nbsp;·&nbsp; no install, no plugins
</p>

<p align="center">
  <img alt="C11" src="https://img.shields.io/badge/C-C11-00599C?logo=c&logoColor=white">
  <img alt="SDL3" src="https://img.shields.io/badge/SDL-3-1A1A1A">
  <img alt="Lua 5.4" src="https://img.shields.io/badge/Lua-5.4-2C2D72?logo=lua&logoColor=white">
  <img alt="WebAssembly" src="https://img.shields.io/badge/WebAssembly-ready-654FF0?logo=webassembly&logoColor=white">
  <img alt="Platforms: Linux, Windows, Web" src="https://img.shields.io/badge/platforms-Linux%20%7C%20Windows%20%7C%20Web-44cc11">
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-green">
</p>

<p align="center">
  <img src="demo.gif" alt="2D game engine benchmark: tens of thousands of sprites simulated and rendered in real time, each repelled by the cursor" width="480">
  <br>
  <sub>Tens of thousands of independently simulated and rendered sprites at interactive frame rates — the engine's built-in stress benchmark.</sub>
</p>

---

**Fire Skeleton** is a lightweight, high-performance **2D game engine in C11**,
built as an **educational project** — a study in data-oriented design and
high-performance C. It pairs a **data-oriented Entity Component System (ECS)** —
built for cache locality and multithreaded simulation of *tens of thousands of
entities* — with an embedded **Lua** scripting layer for gameplay. You write the
hot path in fast C and the rules in flexible Lua, render with **SDL3**, and ship
a single small binary to **Linux, Windows, or the web (WebAssembly)** from one
codebase.

The ECS, every container it relies on, the worker pool, the collision
broad-phase and the logger are custom-built. The only things underneath are
SDL3 (windowing/input/rendering), Lua (scripting) and
[Clay](https://github.com/nicbarker/clay) (UI layout).

## Why this engine

- ⚡ **Fast by design.** A data-oriented ECS with **dense, sparse-set component
  storage** — contiguous arrays, O(1) lookups, no pointer chasing — streamed by a
  **barrier-based multithreaded worker pool**. Simulates *and* renders tens of
  thousands of independent sprites in real time.
- 🪶 **Minimal & dependency-light.** ~7.4k lines of C, no heavy framework. The
  engine core, hashtables, dynamic arrays, memory pool, thread pool and logger
  are all custom-built.
- 🌙 **Lua-scriptable gameplay.** Prefabs, spawning, coroutine-driven wave
  directors, and event callbacks — with **hot reload**. *Script the rules, not
  the inner loop:* heavy per-frame work stays in C, Lua only steers.
- 🎨 **UI in Lua.** An **immediate-mode toolkit** plus a tiny **declarative
  layout** system (HTML-like trees + CSS-like styles), on top of Clay — menus,
  HUD and screens are all just scripts.
- 🌍 **Truly cross-platform.** One source tree builds to **native Linux**, a
  **Windows `.exe`** (MinGW-w64 cross-compile), and **WebAssembly** that runs in
  any modern browser.
- 🔬 **Correctness-checked.** Clean under **AddressSanitizer + LeakSanitizer** —
  no leaks, no memory errors.
- 📊 **Built-in benchmark.** An adaptive ECS stress test that finds and reports
  how many entities your machine sustains.

## Quick start

**Try it now — no install:** [**play the WebAssembly build in your
browser**](https://coolguywbp.github.io/fire-skeleton/).

**Build it natively** (Linux + GCC; needs `gcc`, `make`, `pkg-config` and dev
packages for SDL3, SDL3_image, SDL3_ttf, zlib and Lua 5.4):

```sh
make            # optimized release build -> ./game
make run        # build and run
make debug      # AddressSanitizer + LeakSanitizer build
```

The bundled demo: the mouse navigates the menu; **PLAY** is a small Space
Invaders clone (**← / →** to move, **Space** to shoot); **BENCHMARK** is the
stress test (move the mouse to repel sprites). **Q / Esc** returns to the menu.

## Architecture

A from-scratch ECS, its design inspired by
[sturnclaw/ecs-c](https://github.com/sturnclaw/ecs-c):

- **Entities** are lightweight integer ids.
- **Components** are plain inline data (`Transform`, `Velocity`, `Sprite`) —
  stored in contiguous, packed arrays, not heap pointers.
- **Systems** hold the update logic (`VelocitySystem`, `SpriteRenderSystem`),
  operate on entities matching an **archetype**, and declare ordering
  dependencies.
- The **scheduler** respects those dependencies and fans thread-safe systems
  across the worker pool, while keeping renderer-touching systems on the main
  thread.

It is backed by purpose-built containers (`core/ecs_*.c`): hashtables, a **dense
sparse-set component pool** (contiguous data, O(1) lookups), dynamic arrays, a
memory pool, and a **barrier-based thread pool**.

The game layer is a classic `events → update → render` loop. Scenes are Lua
scripts loaded on demand; each frame the active script emits its UI through the
toolkit, Clay lays it out, and an SDL3 backend (`core/clay_renderer.c`) draws it.

### Why it's fast

The whole design is **data-oriented**: keep the hot per-frame loop touching
tightly packed memory and doing as little as possible per entity.

- **Dense sparse-set component storage.** Each component type lives in one
  contiguous array; a sparse index maps an entity id straight to its slot.
  Access is plain array indexing — **O(1), no hashing, no pointer chasing** — so
  the CPU streams component data with great cache locality. This is the single
  biggest reason it scales.
- **Systems resolve their component pointers once, at registration.** The
  per-entity update is then a couple of array lookups and arithmetic — no
  per-entity hash, no allocation.
- **The hot path stays in C; Lua only steers.** Game logic runs in Lua a handful
  of times per frame (spawning, events, scoring), never once-per-entity, so
  scripting adds essentially nothing to simulating tens of thousands of entities.
- **Opt-in collision.** Only entities with a `CollisionComponent` enter the
  uniform **spatial hash**, so pure-load sprites pay nothing for collision.
- **O(1) churn, no hot-path allocation.** Deletes are swap-removes; the update
  loop allocates nothing per frame.
- **A barrier-based worker pool** fans thread-safe systems across cores, while
  renderer-touching systems stay on the main thread.

Every optimization was driven by the benchmark, not intuition — **measure
before optimizing**: A/B toggling rendering vs. the ECS update showed the cost
was *how component data was reached*, which pointed straight at the storage
layout.

## Scripting (Lua)

Gameplay lives in **Lua** (`scripts/`), embedded via a single main-thread state.
A scene gets a small C API and a few callbacks:

```lua
-- a prefab is a named template -> registers an ECS archetype under the hood
prefab "Invader" {
  Transform = { w = 48, h = 48 },
  Sprite    = { image = "skeleton" },
  Collision = {},                  -- opt in to collision detection
}

function on_start()                -- scene setup
  start(wave)                      -- launch a coroutine-based "director"
end

function wave()                    -- reads top-to-bottom; wait() yields
  for i = 1, 5 do spawn_at("Invader", 100 * i, -20); wait(0.5) end
end

function on_update(dt) ... end     -- once per frame (not per entity)
function on_key(key)  ... end      -- "left" / "right" / "space" / ...
function on_collision(a, b) ... end -- C spatial-hash broad-phase calls back here
```

Exposed to Lua: `prefab`, `spawn` / `spawn_at` / `spawn_many` / `destroy` /
`despawn` / `count`, `set_pos` / `get_pos`, `key_down`, `fps`, `hud`, the
coroutine helpers `start` / `wait`, and `SCREEN_W` / `SCREEN_H`. The complete
surface — callbacks, entities, input, touch, scenes, window/video, the `ui.*`
toolkit and the declarative view runtime — is in the
**[Lua API reference](docs/lua-api.md)**.

**Hot reload:** the active script is watched and a fresh Lua state is swapped in
*only if it loads cleanly* — a syntax error leaves the running game untouched.
Edit a `.lua`, save, and the scene rebuilds with no recompile.

### UI: immediate-mode toolkit + declarative layouts

The interface is drawn from Lua. At the bottom is an **immediate-mode toolkit**
(`ui.panel` / `ui.label` / `ui.button` / `ui.text` / `ui.rect` / `ui.image`)
that emits Clay elements with CSS-like options (per-side padding, `grow` / `fit`
/ percent sizing, borders, colors). On top sits a tiny **declarative view
runtime**: structure and styling live in a layout file (HTML-like tree + CSS-like
stylesheet), the scene file keeps only behavior, and `mount`/`on`/`render` wire
them together.

```lua
-- layout file: structure + style
return { menu = { styles = { item = { size = 54, align = "left" } }, tree = {
  { tag = "button", id = "play", class = "item", text = "PLAY" } } } }

-- scene file: behavior only
mount(require("menu_view").menu):on("play", function() goto_scene("play") end)
```

## Building

One source tree, three targets.

### Linux (native, GCC)

Requires `gcc`, `make`, `pkg-config` and the dev packages for SDL3, SDL3_image,
SDL3_ttf, zlib and Lua 5.4. On Arch, for example:

```sh
sudo pacman -S --needed base-devel sdl3 sdl3_image sdl3_ttf zlib lua
```

```sh
make            # optimized release build (-O3 -march=native -flto) -> ./game
make run        # build and run
make debug      # AddressSanitizer + LeakSanitizer build
make clean
```

### Windows (`.exe`, cross-compiled with MinGW-w64)

The Windows SDL3 stack, Lua 5.4 and zlib are vendored under `vendor/`, so it
builds from a clean clone — you only need the MinGW-w64 toolchain
(`x86_64-w64-mingw32-gcc`).

```sh
make windows      # -> dist/windows/  (game.exe + SDL DLLs + assets/ + scripts/)
make windows-run  # build, then smoke-test under Wine
```

`dist/windows/` is self-contained — copy it to a Windows machine and run
`game.exe`. The renderer is forced to OpenGL on Windows, and the GCC runtime and
winpthreads are linked statically, so only the SDL DLLs ship alongside the exe.

### WebAssembly (Emscripten)

A live build is hosted on GitHub Pages: **[play it in your
browser](https://coolguywbp.github.io/fire-skeleton/)**. The wasm SDL3 stack and
a Lua static lib are vendored, and `assets/` + `scripts/` are baked into the
virtual filesystem, so `git clone` + `make web` works out of the box (needs the
[Emscripten SDK](https://emscripten.org/)).

```sh
make web                                 # -> web/game.{html,js,wasm,data}
python3 -m http.server --directory web   # then open game.html over HTTP
```

The web build is single-threaded (browser threads need SharedArrayBuffer /
COOP-COEP), driven by `requestAnimationFrame`, with a capped benchmark and a
minimal custom canvas shell.

## Footprint

Small on purpose — minimal is a feature.

- ~**7,400** lines of C across **71 files**, plus ~**290** lines of Lua game
  logic.
- ~**40%** of the C is the engine itself: the ECS plus its data structures
  (~2,500 lines — core, hashtables/arrays, the dense component pool, the thread
  pool). The Lua bridge + spatial-hash collision add ~**940** lines.
- The only vendored third-party C is [Clay](https://github.com/nicbarker/clay)
  (`core/clay.h`), the UI layout library.

## Project layout

```
core/
  game.c / main.c           game loop, lifecycle, scenes
  init_*.c                  SDL, Clay and ECS initialization
  ecs*.c                    ECS API, manager, storage, containers, worker pool
  components.c / systems.c  game components and systems
  script.c                  Lua scripting layer (API, prefabs, hot reload)
  collision.c               spatial-hash collision broad-phase
  ui_lua.c / clay_renderer.c  Lua UI toolkit and the SDL3 Clay backend
  clay.h                    vendored Clay library
scripts/                    Lua scenes: menu, Space Invaders demo, benchmark
vendor/                     prebuilt SDL3/Lua libs for the Windows & web builds
```

## Status

A from-scratch engine built as a study in **data-oriented design** and
high-performance C — complete enough to ship a playable demo and a stress
benchmark across three platforms, and still actively evolving. Roadmap ideas:
batched sprite rendering (single `SDL_RenderGeometry` draw), SoA + SIMD motion
updates, a spatial grid for cursor repulsion, and off-screen culling.

## Acknowledgements

- [**sturnclaw/ecs-c**](https://github.com/sturnclaw/ecs-c) — inspiration and
  reference for the ECS architecture.
- [**nicbarker/clay**](https://github.com/nicbarker/clay) — the immediate-mode
  UI layout library (vendored as `core/clay.h`).
- [**Lua**](https://www.lua.org/) — the embedded scripting language.
- [**SDL3**](https://www.libsdl.org/) — windowing, input and rendering.
- [**Liberation Sans**](https://github.com/liberationfonts/liberation-fonts) — a
  free (SIL OFL) Helvetica/Arial-metric font with Cyrillic coverage.

## License

Released under the [MIT License](LICENSE) — permissive and compatible with the
dependencies (SDL3 and Clay are zlib-licensed). `core/clay.h` remains under its
own license.

---

<sub><b>Keywords:</b> 2D game engine · C game engine · lightweight game engine ·
minimal game engine · fast / high-performance game engine · data-oriented design
· Entity Component System (ECS) · SDL3 · Lua scripting · multithreaded ·
cross-platform · WebAssembly · open source · MIT.</sub>
