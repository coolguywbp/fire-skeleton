-- scripts/menu.lua
--
-- The menu scene: behavior only. Structure and styling live in the imported
-- layout file (scripts/menu_view.lua); here we mount each screen and bind its
-- buttons to actions. mount()/view:on()/view:render() come from the engine
-- prelude (the declarative view runtime).

local L = require("menu_view")

-- On the web there's nothing to exit to: a page can't close its own tab, and
-- quitting just stops the render loop (emscripten_cancel_main_loop), which
-- leaves the canvas frozen and looks like a hang. Drop EXIT from the web build.
if IS_WEB then
  local function strip(nodes, id)
    for i = #nodes, 1, -1 do
      if nodes[i].id == id then table.remove(nodes, i)
      elseif nodes[i].children then strip(nodes[i].children, id) end
    end
  end
  strip(L.menu.tree, "exit")
end

local main = mount(L.menu)
  :on("play",    function() goto_scene("demos")   end)
  :on("options", function() goto_scene("options") end)
  :on("exit",    quit)

local opts = mount(L.options)
  :on("video", function() goto_scene("video") end)
  :on("back",  function() goto_scene("menu") end)
  -- audio is a placeholder for now (no handler bound yet).

-- VIDEO screen. Same minimalist look as the other menus (it reuses their
-- stylesheet), but its tree is rebuilt each frame so the labels can reflect the
-- live fullscreen state / active resolution. A trailing dot marks the active
-- mode. The menu is not a hot path, so re-mounting per frame is fine.
local RES = { { 1280, 960 }, { 1600, 1200 }, { 1920, 1080 } }
local DOT = "  \u{00B7}"   -- middle dot, marks the active option
local X   = "\u{00D7}"     -- multiplication sign (compact "WxH")

local function video_view()
  local fs = is_fullscreen()
  local cw, ch = get_window_size()

  local items = {
    { tag = "button", id = "fs", class = "item",
      text = "FULLSCREEN" .. (fs and DOT or "") },
  }
  if not IS_WEB then
    for i, r in ipairs(RES) do
      local sel = (not fs) and cw == r[1] and ch == r[2]
      items[#items + 1] = { tag = "button", id = "res" .. i, class = "item",
                            text = r[1] .. X .. r[2] .. (sel and DOT or "") }
    end
  end
  items[#items + 1] = { tag = "button", id = "back", class = "item", text = "BACK" }

  local v = mount({
    styles = L.menu.styles,
    tree = {
      { tag = "panel", class = "screen", children = {
        { tag = "panel", class = "titlebox", children = {
          { tag = "label", class = "optitle", text = "VIDEO" },
        } },
        -- Wider than the shared 300px list so "WxH" labels never wrap.
        { tag = "list", class = "list", style = { width = 480 }, children = items },
      } },
    },
  })
  v:on("fs",   function() set_fullscreen(not is_fullscreen()) end)
  v:on("back", function() goto_scene("options") end)
  for i, r in ipairs(RES) do
    v:on("res" .. i, function() set_window_size(r[1], r[2]) end)
  end
  return v
end

-- The bobbing fire-skeleton is animated (driven by time()), so it stays in the
-- scene rather than the static layout: image 0, 450x644, anchored to the right
-- (right edge 100px from the screen edge).
local function skeleton()
  local off = math.sin(time() * 2.0) * 50
  local w, h = 450, 644
  ui.image("skeleton", SCREEN_W - w - 100, 150 + off, w, h)
end

-- Demo picker: a tile grid (3 across). Each demo is a thumbnail (a screenshot
-- captured 5 s into the demo, loaded as an image id -- see load_i.c) with its
-- name below; BACK is the trailing tile. Built imperatively because a tile is an
-- image + caption + click target, which the text-only declarative buttons don't
-- cover. Mouse/touch hit the ui.button under each tile; the keyboard moves a 2D
-- cursor (dgc) over the same cells.
local DEMOS = {
  { key = "invaders",  name = "INVADERS",  shot = "shot_invaders",  go = "play"      },
  { key = "slots",     name = "SLOTS",     shot = "shot_slots",     go = "slots"     },
  { key = "benchmark", name = "BENCHMARK", shot = "shot_benchmark", go = "benchmark" },
  { key = "cube",      name = "3D CUBE",   shot = "shot_cube",      go = "cube"      },
}
local dgc = 1   -- demos grid cursor: 1..#DEMOS = a demo, #DEMOS+1 = BACK

local function demo_count() return #DEMOS + 1 end

-- Columns adapt to the (adaptive) width: 3 across when there's room, 2 on a
-- narrow/portrait phone, so the tiles always fit and stay tappable instead of
-- running off-screen.
local function demo_cols() return (SCREEN_W < 1140) and 2 or 3 end

local function demo_activate(i)
  if i >= 1 and i <= #DEMOS then goto_scene(DEMOS[i].go)
  else goto_scene("menu") end   -- BACK
end

local function draw_demos()
  ui.rect(0, 0, SCREEN_W, SCREEN_H, { color = { 0, 0, 0, 255 } })

  local n     = demo_count()
  local cols  = demo_cols()
  local gap   = 28
  local marg  = 70
  local tw    = math.min(360, (SCREEN_W - marg * 2 - (cols - 1) * gap) / cols)
  local imgh  = math.floor(tw * 9 / 16)          -- 16:9 thumbnail
  local nsize = math.max(16, math.min(28, tw * 0.09))
  local th    = imgh + nsize + 24
  local rows  = math.ceil(n / cols)
  local gridw = cols * tw + (cols - 1) * gap
  local gridh = rows * th + (rows - 1) * gap
  local sx    = (SCREEN_W - gridw) / 2
  local WHITE = { 255, 255, 255, 255 }

  -- Centre the title + grid block vertically.
  local tsize = math.min(90, SCREEN_W * 0.11)
  local top   = math.max(20, (SCREEN_H - gridh - tsize - 40) / 2)
  ui.text(SCREEN_W / 2 - (#"DEMOS" * tsize * 0.6) / 2, top, "DEMOS", { size = tsize, font = 1 })
  local sy = top + tsize + 40

  for i = 1, n do
    local col = (i - 1) % cols
    local row = math.floor((i - 1) / cols)
    local x   = sx + col * (tw + gap)
    local y   = sy + row * (th + gap)
    -- Monochrome to match the rest of the menu: no fill, a thin white frame that
    -- thickens on hover / keyboard selection.
    local frame = { color = { 0, 0, 0, 0 }, hover_color = { 0, 0, 0, 0 },
                    border = 2, border_hot = 6, border_color = WHITE,
                    radius = 4, selected = (dgc == i) }
    local d = DEMOS[i]
    if d then
      if ui.button(d.key, x, y, tw, th, "", frame) then demo_activate(i) end
      ui.image(d.shot, x + 6, y + 6, tw - 12, imgh, 870)   -- z above the tile frame
      local cw = #d.name * nsize * 0.62                     -- approx centre (no text metrics)
      ui.text(x + (tw - cw) / 2, y + imgh + 12, d.name, { size = nsize, font = 1 })
    else
      frame.size = math.max(24, nsize + 6)
      if ui.button("back", x, y, tw, th, "BACK", frame) then demo_activate(i) end
    end
  end
end

-- Keyboard navigation. The selected item per screen is remembered (cursor for
-- the declarative menus, dgc for the demos grid) and reset on entering a screen.
-- The video screen is rebuilt every frame, so its cursor lives here rather than
-- on the (ephemeral) view object and is applied to each fresh mount.
local cursor = {}
local active = nil

local function enter(s)
  if s ~= active then active = s; cursor[s] = 1; dgc = 1 end
end

local function view_for(s)
  if s == "options" then return opts
  elseif s == "video" then return video_view()
  else return main end
end

function on_key(key)
  local s = scene()
  enter(s)
  if s == "demos" then
    local n, cols = demo_count(), demo_cols()
    if     key == "left"  then dgc = math.max(1, dgc - 1)
    elseif key == "right" then dgc = math.min(n, dgc + 1)
    elseif key == "up"    then if dgc - cols >= 1 then dgc = dgc - cols end
    elseif key == "down"  then if dgc + cols <= n then dgc = dgc + cols end
    elseif key == "return" or key == "keypad enter" then demo_activate(dgc) end
    return
  end
  local v = view_for(s)
  v.cursor = cursor[s] or 1
  if key == "up" then
    v:nav(-1)
  elseif key == "down" then
    v:nav(1)
  elseif key == "return" or key == "keypad enter" then
    v:activate()   -- may switch scenes; the new one resets via enter() next frame
    return
  else
    return
  end
  cursor[s] = v.cursor
end

function on_ui()
  local s = scene()
  enter(s)
  if s == "demos" then
    draw_demos()   -- its own full-screen background; no floating skeleton (no room)
    return
  end
  local v = view_for(s)
  v.cursor = cursor[s] or 1
  v:render()
  skeleton()   -- the bobbing fire-skeleton appears on the other menu screens
end
