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

- **Coordinate space.** All positions and sizes are in a **logical** space
  (`SCREEN_W` × `SCREEN_H`) that the renderer scales to the real
  window/canvas/fullscreen size, mapping mouse/touch input back into it — so
  scripts never deal with the actual resolution. The **height is fixed at 960**;
  the **width is adaptive**, tracking the window's aspect ratio (≈ `1280` at 4:3,
  wider on a 16:9 screen, narrower on an upright phone). Read `SCREEN_W` each
  frame rather than assuming a constant — it changes on resize / rotation, and a
  scene can detect portrait with `SCREEN_W < SCREEN_H`.
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
| `"cube"` | 3D cube demo (software `g3d` renderer) |

```lua
:on("play", function() goto_scene("play") end)
if scene() == "video" then draw_video() end
```

Pressing `Esc`/`Q` inside a demo returns to the **demo picker**; from a menu
screen it returns to the **main menu** (handled by the engine).

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
| `SCREEN_W` | logical width — **adaptive** (≈ `1280`, tracks the window aspect ratio; updated on resize) |
| `SCREEN_H` | `960` (logical height — fixed) |
| `IS_WEB` | `true` on the WebAssembly build, else `false` |

```lua
local off = math.sin(time() * 2.0) * 50          -- bobbing animation
hud(string.format("BENCHMARK: %d (%d fps)", n, math.floor(fps())))
local cap = IS_WEB and 3000 or 100000
local portrait = SCREEN_W < SCREEN_H             -- phone held upright
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

### `ui.image(image, x, y, w, h [, zIndex])`

Draw a loaded [image](#images) at `(x, y)` sized `w × h`. `image` is a friendly
name string (preferred) or a raw id. The optional `zIndex` (default `750`) lifts
it above other layers — the demo picker uses `870` so a screenshot sits over its
tile button.

### `ui.button(...)` — two forms

Returns `true` on the frame it is clicked/tapped.

**Floating** (absolute) — pass numeric coords:

```lua
ui.button(id, x, y, w, h, label [, opts])
```

| opt | default | |
|---|---|---|
| `size` | `40` | label font size |
| `color` | `{40,40,60,255}` | background |
| `hover_color` | `{70,70,110,255}` | background when hovered/selected |
| `text_color` | white | |
| `border` | `0` | base border width (0 = none) |
| `border_hot` | `border` | border width when hovered or `selected` |
| `border_color` | white | |
| `radius` | `8` | corner radius |
| `selected` | `false` | force the hovered look (for keyboard selection) |

The thin-frame menu look (tiles, portrait buttons) uses a transparent `color`
with `border`/`border_hot` so the white frame thickens on hover or when
`selected` is set by the keyboard cursor.

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

The menus are authored declaratively, as a **tree of nodes + a stylesheet**
(think "HTML + CSS"), kept in a separate `*_view.lua` layout file. A scene script
imports that file, `mount`s each screen, binds its button ids to actions, and
calls `view:render()` each frame.

### Is this a Clay interface?

Not directly — it's two layers above Clay:

```
Clay  (C immediate-mode layout engine: boxes, flexbox-style sizing, z-order)
  ▲
ui.*  (thin Lua bindings — each call emits exactly one Clay element this frame)
  ▲
view runtime  (pure Lua: walks a node tree and emits the matching ui.* calls)
```

The view runtime never touches Clay. It only translates each node into an
`ui.panel` / `ui.label` / `ui.button` / … call (see
[the toolkit above](#immediate-mode-ui-ui)); those bindings are what talk to
Clay. So the declarative tree is sugar over the immediate-mode toolkit, which is
itself a small façade over Clay. Anything the tree can express, you could write
by hand as `ui.*` calls — and where the tree model doesn't fit, you do exactly
that (see [Escape hatch](#escape-hatch-imperative-ui)).

This layering is deliberate: the layout file holds **structure + look**, the
scene file holds **behavior**. Neither knows the other's internals; they meet
only through button **ids**.

### The split, end to end

```lua
-- scripts/menu_view.lua  — structure + styles, no behavior. Returns a table of
-- screens; each screen is a { styles = ..., tree = ... } spec.
local styles = {
  screen = { width = "grow", height = "grow", dir = "column",
             pad = { top = 200, left = 100 }, gap = 64, color = { 0, 0, 0, 255 } },
  title  = { size = 130, font = 1 },
  list   = { width = 300, height = "grow", dir = "column" },
  item   = { size = 54, height = "grow", align = "left" },
}
return {
  menu = {
    styles = styles,
    tree = {
      { tag = "panel", class = "screen", children = {
        { tag = "label",  class = "title", text = "FIRE SKELETON" },
        { tag = "list",   class = "list", children = {
          { tag = "button", id = "play", class = "item", text = "PLAY" },
          { tag = "button", id = "exit", class = "item", text = "EXIT" },
        } },
      } },
    },
  },
}
```

```lua
-- scripts/menu.lua  — behavior. Mount once, bind ids, render each frame.
local L = require("menu_view")
local main = mount(L.menu)
  :on("play", function() goto_scene("demos") end)   -- chainable
  :on("exit", quit)

function on_ui()  main:render()  end
```

### API

| Function | Description |
|---|---|
| `mount(spec)` | Build a view from `spec.tree` + `spec.styles`. Collects button ids (in tree order) for keyboard nav. Returns the view. |
| `view:on(id, fn)` | Bind button `id` to a click/activate handler. Returns the view (chainable). |
| `view:render()` | Walk the tree and emit it this frame. Call inside `on_ui()`. |
| `view:nav(d)` | Move the keyboard cursor by `d` (`-1`/`+1`) over the view's buttons, with wraparound. |
| `view:activate()` | Fire the handler under the cursor (e.g. on Enter). |
| `view.cursor` | Field: the 1-based index of the highlighted button. Set it before `render()`; the cursored button is auto-bolded. |
| `style(a, b, ...)` | Merge style tables left-to-right (later overrides earlier); returns a new table. Useful for building style variants. |

### Nodes

A node is a table: `{ tag = ..., class = ..., ... }`. The `tag` picks the
renderer; everything else is fields.

| `tag` | Renders as | In-flow? | Relevant fields |
|---|---|---|---|
| `panel` | `ui.panel` container | yes | `class`, `style`, `children` |
| `list` | same as `panel` | yes | `class`, `style`, `children` |
| `label` | `ui.label` | yes | `text`, `class`, `style` |
| `button` | `ui.button` (in-flow) | yes | `id`, `text`, `class`, `style` |
| `image` | `ui.image` | **no — floating** | `image`, `x`, `y`, `w`, `h` |
| `text` | `ui.text` | **no — floating** | `text`, `x`, `y`, `class`, `style` |
| `rect` | `ui.rect` | **no — floating** | `x`, `y`, `w`, `h`, `class`, `style` |

`panel`/`list`/`label`/`button` participate in Clay's flow layout (parents size
and arrange them via `dir`/`gap`/`pad`/`align`). `image`/`text`/`rect` are
**floating**: they ignore the surrounding flow and are placed at absolute logical
`(x, y)` — so put those coordinates on the node, not in its class. (`list` is
just an alias for `panel`; the two are identical, named apart only for
readability.)

### Styling: the cascade

A node's final style is `styles[class]` **cascaded with** any inline
`style = { ... }` on the node (inline keys win). A node with no `class` and no
`style` gets an empty style (toolkit defaults apply).

```lua
{ tag = "list", class = "list", style = { width = 480 }, children = items }
--                     ^ base from stylesheet      ^ this overrides just width
```

The full set of style keys (sizing, layout, padding, text, appearance) is the
same one the toolkit accepts — see [Style reference](#style-reference).

### Keyboard navigation

`mount` walks the tree once and records every `button` id **in tree order** into
`view.buttons`. That ordered list is what the cursor moves over:

- `view.cursor` is a 1-based index into it; `render()` bolds the button at that
  index (`font = 1`), matching the mouse-hover look so both input methods read
  the same.
- `view:nav(-1)` / `view:nav(1)` move the cursor up/down with wraparound.
- `view:activate()` fires the handler bound to the cursored id (does nothing if
  that id has no handler).

Mouse/touch clicks work independently of the cursor — a click on any button
fires its handler regardless of where the cursor is. The scene owns cursor
**persistence**: `scripts/menu.lua` keeps a `cursor[sceneName]` table, restores
it into `view.cursor` before `render()`, and resets it on entering a screen.

```lua
function on_key(key)
  local v = current_view()
  v.cursor = cursor[scene] or 1
  if     key == "up"   then v:nav(-1)
  elseif key == "down" then v:nav(1)
  elseif key == "return" then v:activate(); return end
  cursor[scene] = v.cursor
end
```

### Dynamic views: re-mount per frame

`mount` is cheap (the menu is not a hot path), so a screen whose **content**
changes each frame can just be rebuilt every frame instead of mutated. The VIDEO
screen does this to reflect live fullscreen state / the active resolution: it
constructs the tree, `mount`s it, binds handlers, and returns the fresh view —
all inside `on_ui`'s helper. Re-mounting also re-collects the button list, so
keyboard nav stays correct as items appear/disappear.

You can also **mutate the tree before mounting** for static, build-time
conditionals — e.g. stripping the EXIT button on the web build (a page can't
close its own tab) before the one-time `mount`.

### Escape hatch: imperative UI

The tree covers text-and-box menus. When a screen needs something it can't
express — a thumbnail tile (image + caption + click target as one cell), a
free-form responsive layout, per-item geometry — drop to the `ui.*` toolkit
directly inside `on_ui()`. The demo picker (a screenshot tile grid) and the
portrait main menu are built this way; they share the same `ui.button` /
`ui.image` / `ui.text` primitives the view runtime emits, just driven by your
own loop and math. Declarative and imperative screens coexist in one scene file —
branch on `scene()` and render whichever fits.

### Engine-injected BACK button

In **level scenes on the web build**, the engine itself draws an on-screen BACK
button each frame (the prelude's `__ui_back`, bottom-left) so touch devices with
no Esc key can leave a demo. You don't add it to your tree — it's automatic, web
+ level scenes only. Desktop uses Esc/Q instead (handled in C).

### Gotchas

- **Buttons need a unique `id`.** It's both the Clay element id (hit-testing) and
  the handler key. A `button` with no `id`, or whose `id` has no `:on` handler,
  renders and highlights but does nothing when clicked/activated.
- **Floating nodes don't flow.** `image`/`text`/`rect` are positioned absolutely;
  they won't be laid out by a parent panel. Mixing them into a `children` list is
  fine, but their `x`/`y` are logical-space coordinates, not offsets within the
  parent.
- **Coordinates are adaptive.** `SCREEN_W` changes with the window aspect ratio
  (see [Conventions](#conventions)); compute absolute positions from
  `SCREEN_W`/`SCREEN_H` rather than hard-coding, or the layout drifts off-centre
  in fullscreen / on phones.

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

Images are **auto-discovered**: at startup the engine scans `assets/images/` for
every `*.png`, sorts them by filename, and loads each as a texture. There is no
manifest to edit and no fixed id list — **to add an image, drop a `.png` into
`assets/images/`** and address it by its filename stem (the name without
`.png`).

`shot_cube.png` → `"shot_cube"`, `skeleton.png` → `"skeleton"`. Names are how
both sprites and `ui.image` refer to images:

```lua
prefab "Player" { Transform = { w = 64, h = 64 }, Sprite = { image = "skeleton" } }
ui.image("skeleton", SCREEN_W - 550, 150, 450, 644)    -- by name
ui.image("shot_cube", x, y, w, h, 870)                 -- thumbnail above a tile
```

Raw numeric ids still work (an image's id is its index in the sorted scan), but
they shift whenever files are added or renamed — **prefer names**. An unknown
name resolves to id `0` for sprites; `ui.image` with an out-of-range id draws
nothing.

The current bundled set: `skeleton`, `sprite`, and the demo-picker thumbnails
`shot_invaders` / `shot_slots` / `shot_benchmark` / `shot_cube`.
