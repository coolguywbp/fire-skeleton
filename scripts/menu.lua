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
  ui.image(0, SCREEN_W - w - 100, 150 + off, w, h)
end

-- Demo picker: a tile grid (3 across). Each demo is a thumbnail (a screenshot
-- captured 5 s into the demo, loaded as an image id -- see load_i.c) with its
-- name below; BACK is the trailing tile. Built imperatively because a tile is an
-- image + caption + click target, which the text-only declarative buttons don't
-- cover. Mouse/touch hit the ui.button under each tile; the keyboard moves a 2D
-- cursor (dgc) over the same cells.
local DEMOS = {
  { key = "invaders",  name = "INVADERS",  shot = 2, go = "play"      },
  { key = "slots",     name = "SLOTS",     shot = 3, go = "slots"     },
  { key = "benchmark", name = "BENCHMARK", shot = 4, go = "benchmark" },
  { key = "cube",      name = "3D CUBE",   shot = 5, go = "cube"      },
}
local DCOLS = 3
local dgc = 1   -- demos grid cursor: 1..#DEMOS = a demo, #DEMOS+1 = BACK

local function demo_count() return #DEMOS + 1 end

local function demo_activate(i)
  if i >= 1 and i <= #DEMOS then goto_scene(DEMOS[i].go)
  else goto_scene("menu") end   -- BACK
end

local function draw_demos()
  ui.rect(0, 0, SCREEN_W, SCREEN_H, { color = { 0, 0, 0, 255 } })
  ui.text(SCREEN_W / 2 - 140, 80, "DEMOS", { size = 90, font = 1 })

  local tw, th, gap, imgh = 360, 254, 32, 194
  local n     = demo_count()
  local gridw = DCOLS * tw + (DCOLS - 1) * gap
  local sx    = (SCREEN_W - gridw) / 2
  local sy    = 300
  local WHITE = { 255, 255, 255, 255 }

  for i = 1, n do
    local col = (i - 1) % DCOLS
    local row = math.floor((i - 1) / DCOLS)
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
      ui.image(d.shot, x + 8, y + 8, tw - 16, imgh, 870)   -- z above the tile frame
      local cw = #d.name * 28 * 0.62                        -- approx centre (no text metrics)
      ui.text(x + (tw - cw) / 2, y + imgh + 16, d.name, { size = 28, font = 1 })
    else
      frame.size = 40
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
    local n = demo_count()
    if     key == "left"  then dgc = math.max(1, dgc - 1)
    elseif key == "right" then dgc = math.min(n, dgc + 1)
    elseif key == "up"    then if dgc - DCOLS >= 1 then dgc = dgc - DCOLS end
    elseif key == "down"  then if dgc + DCOLS <= n then dgc = dgc + DCOLS end
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
