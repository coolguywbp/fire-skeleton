# Fire Skeleton — Lua API Reference

Every scene in the engine (menus and gameplay alike) is driven by a Lua script.
The C engine loads the script for the current scene, calls a set of **lifecycle
callbacks** on it each frame, and exposes a **global API** of functions the
script can call back into (spawn entities, draw UI, read input, change scenes,
…).

This document covers the complete surface available to scripts.

- [Conventions](#conventions)
- [Lifecycle callbacks](#lifecycle-callbacks)
- [Entities & prefabs](#entities--prefabs)
- [Input](#input)
- [Coroutine scheduler](#coroutine-scheduler)
- [Scenes & navigation](#scenes--navigation)
- [Window & video](#window--video)
- [Misc functions & globals](#misc-functions--globals)
- [Immediate-mode UI (`ui.*`)](#immediate-mode-ui-ui)
- [Declarative view runtime](#declarative-view-runtime)
- [Style reference](#style-reference)
- [Images](#images)

---

## Conventions

- **Coordinate space.** All positions and sizes are in a fixed **logical
  1280×960** space (`SCREEN_W` × `SCREEN_H`). The renderer letterbox-scales this
  to the real window/canvas/fullscreen size, and maps mouse/touch input back
  into it — so scripts never deal with the actual resolution.
- **Where scripts live.** One script per scene under `scripts/`. They can
  `require` each other (e.g. a scene imports its layout file):
  `package.path` already includes `scripts/?.lua`.
- **Hot reload.** Saving a script reloads it live; redefining a `prefab` is
  supported.
- **Units.** Colors are `{r, g, b, a}` with components `0–255` (alpha defaults
  to `255`). Angles/time are in seconds.

---

## Lifecycle callbacks

Define any of these as globals in a scene script; the engine calls the ones it
finds.

| Callback | When | Notes |
|---|---|---|
| `on_start()` | Once, when the scene's script loads | Set up state, spawn the initial entities. |
| `on_update(dt)` | Every frame | `dt` = seconds since last frame. Game logic. |
| `on_key(key)` | On a key press/repeat event | **Gameplay scenes only** (menus handle their own input). `key` is a friendly lowercase name — see [Input](#input). |
| `on_collision(a, b)` | When two entities with `Collision` overlap | `a`, `b` are entity ids. Runs after movement, on the main thread. |
| `on_ui()` | Every frame, during layout | Draw the immediate-mode UI / declarative views here. Runs in **all** scenes. |

```lua
function on_start()  player = spawn_at("Player", 100, 100) end
function on_update(dt)  if key_down("right") then move(dt) end end
function on_ui()  ui.text(16, 16, "SCORE " .. score, { size = 30 }) end
```

> Held-key movement belongs in `on_update` via `key_down()`, not `on_key`:
> `on_key` follows the OS key-repeat stream and can be interrupted by other keys.

---

## Entities & prefabs

The engine has a small ECS. A **prefab** is a named template (a set of
components + default field values); spawning one creates an entity and applies
those defaults.

### Defining a prefab

```lua
prefab "Player" {
  Transform = { w = 64, h = 64 },     -- x/y default to the C constructor
  Sprite    = { image = "skeleton" }, -- image id (number) or friendly name
  Collision = {},                     -- presence opts into collision detection
}
```

`prefab "Name" { ... }` is sugar for `prefab("Name")({ ... })`. Components:

| Component | Fields | Omitted field → |
|---|---|---|
| `Transform` | `x`, `y`, `w`, `h` | constructor default (e.g. random position) |
| `Velocity`  | `vx`, `vy` | constructor default (random velocity) |
| `Sprite`    | `image` (number id or name string) | image 0 |
| `Collision` | *(none — presence only)* | — |

A prefab must declare at least one known component. An **empty** component table
(`Velocity = {}`) keeps the C constructor's defaults and costs nothing per
spawn — this is what makes bulk spawning cheap.

### Spawning & lifetime

| Function | Returns | Description |
|---|---|---|
| `spawn(name)` | entity id | Create one entity from the prefab. |
| `spawn_at(name, x, y)` | entity id | Spawn and place at `(x, y)`. |
| `spawn_many(name, n)` | — | Bulk-spawn `n` (no ids). Cheap load (used by the benchmark). |
| `destroy(e)` | — | Delete entity `e` (idempotent). |
| `despawn(name, n)` | removed count | Destroy up to `n` of the prefab's entities (most-recent first). |
| `count(name)` | number | Live entities from this prefab (prunes dead ones first). |
| `set_pos(e, x, y)` | — | Move entity `e` (writes its `Transform`). |
| `get_pos(e)` | `x, y` | Position, or `nil` if `e` has no `Transform`. |

```lua
local id = spawn_at("Bullet", x, y)
set_pos(id, x, y - 5)
if count("Invader") == 0 then next_wave() end
destroy(id)
```

Rendering (the `SpriteRenderSystem`) and collision detection are handled by the
C side; movement is authored in Lua and pushed to the ECS via `set_pos`.

---

## Input

### Keyboard

- **`key_down(name) -> bool`** — live key state, for held-key movement in
  `on_update`. Returns `true` while the key is physically held.
- **`on_key(key)`** — discrete press/repeat events (gameplay scenes only).

Recognized key names (lowercase): `"left"`, `"right"`, `"up"`, `"down"`,
`"space"`, and any single letter `"a"`–`"z"`.

```lua
function on_update(dt)
  if key_down("left")  or key_down("a") then px = px - speed * dt end
  if key_down("space") then fire() end
end
```

### Touch (mobile / web)

Coordinates are in logical space, so they line up with what you draw.

- **`touch_count() -> n`** — number of active touch points.
- **`touch_pos(i) -> x, y`** — position of the `i`-th active touch (1-based), or
  `nil` if there is no such touch.

```lua
if touch_count() > 0 then
  local tx = touch_pos(1)          -- track the first finger's x
  if tx then px = tx - PLAYER_W / 2 end
end
```

Taps on `ui.button`s work automatically (a touch drives the UI pointer).

---

## Coroutine scheduler

A tiny coroutine director for timed sequences (wave timers, delays). The engine
ticks it once per frame.

- **`start(fn, ...)`** — run `fn` as a coroutine; its first segment runs
  immediately. Extra args are passed to `fn`.
- **`wait(sec)`** — inside a coroutine, pause it for `sec` seconds.

```lua
-- Clear the wave, pause 1.5s, then spawn the next one.
start(function()
  wait(1.5)
  next_wave()
end)
```

---

## Scenes & navigation

- **`scene() -> name`** — the current scene.
- **`goto_scene(name)`** — switch scenes.

Valid names:

| Name | Scene |
|---|---|
| `"menu"` | Main menu |
| `"options"` | Options screen |
| `"demos"` | Demo picker (PLAY) |
| `"video"` | Video settings |
| `"play"` | Invaders demo |
| `"benchmark"` | Benchmark demo |
| `"slots"` | Slots demo |

```lua
:on("play", function() goto_scene("play") end)
if scene() == "video" then draw_video() end
```

Pressing `Esc` returns to the main menu (handled by the engine).

---

## Window & video

The world is resolution-independent; these change the actual window. The logical
space is unaffected.

| Function | Returns | Description |
|---|---|---|
| `set_fullscreen(on)` | — | Toggle borderless desktop fullscreen (`on` is truthy/falsey). |
| `is_fullscreen()` | bool | Current fullscreen state. |
| `set_window_size(w, h)` | — | Leave fullscreen, resize and re-center the window. |
| `get_window_size()` | `w, h` | Current window size in pixels (not the logical size). |

```lua
if ui.button("fs", x, y, w, h, "FULLSCREEN") then
  set_fullscreen(not is_fullscreen())
end
```

> On the web build the canvas tracks the browser viewport, so `set_window_size`
> doesn't apply there (use `IS_WEB` to hide it).

---

## Misc functions & globals

| Function | Returns | Description |
|---|---|---|
| `log(msg)` | — | Write a line to the engine log (prefixed `[lua]`). |
| `time()` | seconds | Time since start, for animations. |
| `fps()` | number | Current smoothed frame rate. |
| `hud(text)` | — | Set the one-line status text (shown top-left, gameplay scenes). |
| `quit()` | — | Exit the game. |

Globals:

| Global | Value |
|---|---|
| `SCREEN_W` | `1280` (logical width) |
| `SCREEN_H` | `960` (logical height) |
| `IS_WEB` | `true` on the WebAssembly build, else `false` |

```lua
local off = math.sin(time() * 2.0) * 50          -- bobbing animation
hud(string.format("BENCHMARK: %d (%d fps)", n, math.floor(fps())))
local cap = IS_WEB and 3000 or 100000
```

---

## Immediate-mode UI (`ui.*`)

Call these inside `on_ui()`; they emit one element per call, every frame.
`opts` is an optional table; colors are `{r, g, b, a}` (0–255).

### `ui.text(x, y, str [, opts])`

Floating text at logical `(x, y)`.

| opt | default | |
|---|---|---|
| `size` | `24` | font size |
| `color` | white | |
| `font` | `0` | `0` = regular, `1` = bold |

### `ui.rect(x, y, w, h [, opts])`

Filled rectangle.

| opt | default | |
|---|---|---|
| `color` | `{0,0,0,180}` | |
| `radius` | `0` | corner radius |

### `ui.image(imageId, x, y, w, h)`

Draw a loaded [image](#images) at `(x, y)` sized `w × h`.

### `ui.button(...)` — two forms

Returns `true` on the frame it is clicked/tapped.

**Floating** (absolute) — pass numeric coords:

```lua
ui.button(id, x, y, w, h, label [, opts])
```

| opt | default |
|---|---|
| `size` | `40` |
| `color` | `{40,40,60,255}` |
| `hover_color` | `{70,70,110,255}` |
| `text_color` | white |

**In-flow** (inside a `ui.panel`) — pass a string label as the 2nd arg:

```lua
ui.button(id, label [, opts])
```

Text-only by default (bolds on hover, like the classic menu). opts: `size`
(`54`), `font` (`0`), `color` (box bg, transparent by default), `text_color`,
`align` (`"left"|"center"|"right"`), `width`/`height`, `pad`, `radius`,
`border` — see [Style reference](#style-reference).

```lua
if ui.button("restart", SCREEN_W/2 - 110, SCREEN_H/2 + 36, 220, 64, "RESTART", { size = 36 }) then
  new_game()
end
```

### `ui.panel(opts, fn)` and `ui.label(text [, opts])`

`ui.panel` is a layout container; `fn` emits its children. `ui.label` is in-flow
text inside a panel. Both are normally used via the declarative runtime below,
but are available directly.

- `ui.panel` opts: `dir` (`"row"|"column"`, default `column`), `align_x`,
  `align_y`, `gap`, `pad`, `width`, `height`, `color`, `radius`, `border`.
- `ui.label` opts: `size` (`24`), `color` (white), `font` (`0`), `line`
  (line height, `0` = auto).

---

## Declarative view runtime

For menus, structure/look can be authored as a tree + stylesheet (think
"HTML + CSS"), kept separate from behavior. The walker maps each node to the
`ui.*` calls above.

```lua
local view = mount({
  styles = { ... },          -- class name -> style table (see below)
  tree   = { ... },          -- nodes
})
view:on("play", function() goto_scene("play") end)   -- bind a button id -> action
                            -- (chainable: :on(...):on(...))
function on_ui() view:render() end
```

| Function | Description |
|---|---|
| `mount(spec)` | Build a view from `spec.tree` + `spec.styles`. Returns the view. |
| `view:on(id, fn)` | Bind button `id` to `fn`. Returns the view (chainable). |
| `view:render()` | Emit the tree this frame. |
| `style(a, b, ...)` | Merge style tables left-to-right (later overrides earlier); returns a new table. |

### Nodes

A node is a table: `{ tag = ..., class = ..., ... }`.

| `tag` | Renders as | Relevant fields |
|---|---|---|
| `panel` | `ui.panel` container | `class`, `style`, `children` |
| `list` | same as `panel` | `class`, `style`, `children` |
| `label` | `ui.label` | `text`, `class`, `style` |
| `button` | `ui.button` (in-flow) | `id`, `text`, `class`, `style` |
| `image` | `ui.image` | `image`, `x`, `y`, `w`, `h` |
| `text` | `ui.text` | `text`, `x`, `y`, `class`, `style` |
| `rect` | `ui.rect` | `x`, `y`, `w`, `h`, `class`, `style` |

A node's final style is its `class` (looked up in `styles`) cascaded with any
inline `style = { ... }` (inline wins).

```lua
local styles = {
  screen = { width = "grow", height = "grow", dir = "column",
             pad = { top = 200, left = 100 }, gap = 64, color = { 0, 0, 0, 255 } },
  title  = { size = 130, font = 1 },
  list   = { width = 300, height = "grow", dir = "column" },
  item   = { size = 54, height = "grow", align = "left" },
}
local tree = {
  { tag = "panel", class = "screen", children = {
    { tag = "label",  class = "title", text = "FIRE SKELETON" },
    { tag = "list",   class = "list", children = {
      { tag = "button", id = "play", class = "item", text = "PLAY" },
      { tag = "button", id = "exit", class = "item", text = "EXIT" },
    } },
  } },
}
```

---

## Style reference

Style tables (stylesheet classes or inline `style`) accept these keys. They map
to `ui.panel` / `ui.label` / `ui.button` options.

**Sizing** — `width`, `height`:

| value | meaning |
|---|---|
| a number | fixed pixels |
| `"grow"` | fill available space |
| `"fit"` | size to content (default) |
| `"NN%"` | percent of the parent |

**Layout** (panels/lists):

| key | values | default |
|---|---|---|
| `dir` | `"row"`, `"column"` | `column` |
| `align_x` | `"left"`, `"center"`, `"right"` | `left` |
| `align_y` | `"top"`, `"center"`, `"bottom"` | `top` |
| `gap` | number | `0` |
| `pad` | see below | `0` |

**Padding** (`pad`) — a number (all sides), CSS positional shorthand, or named:

```lua
pad = 8                      -- all sides
pad = { 10, 20 }             -- { vertical, horizontal }
pad = { 10, 20, 30 }         -- { top, horizontal, bottom }
pad = { 10, 20, 30, 40 }     -- { top, right, bottom, left }
pad = { top = 200, left = 100 }   -- named (any subset)
```

**Text** (labels/buttons): `size`, `color`, `font` (`0` regular, `1` bold),
`line` (line height; `0` = auto), `align` (`"left"|"center"|"right"`).

**Appearance**: `color` (`{r,g,b,a}`), `radius` (corner radius), and
`border = { width = N, color = {r,g,b,a} }`.

---

## Images

Sprites and `ui.image` take an image id. Friendly names map to ids:

| id | names | asset |
|---|---|---|
| `0` | `"skeleton"`, `"menu"` | `MenuFireSkeleton.png` |
| `1` | `"sheet"`, `"sprite"` | `SpriteSheet1.png` |

```lua
prefab "Player" { Transform = { w = 64, h = 64 }, Sprite = { image = "skeleton" } }
ui.image(0, SCREEN_W - 550, 150, 450, 644)     -- by id
```
