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

-- PLAY opens this picker; each entry launches a demo scene.
local demos = mount(L.demos)
  :on("invaders",  function() goto_scene("play")      end)
  :on("slots",     function() goto_scene("slots")     end)
  :on("benchmark", function() goto_scene("benchmark") end)
  :on("back",      function() goto_scene("menu")      end)

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

-- Keyboard navigation. The selected item per screen is remembered in `cursor`
-- (keyed by scene name) and reset to the top whenever we enter a new screen.
-- The video screen is rebuilt every frame, so its cursor lives here rather than
-- on the (ephemeral) view object and is applied to each fresh mount.
local cursor = {}
local active = nil

local function view_for(s)
  if s == "options" then return opts
  elseif s == "video" then return video_view()
  elseif s == "demos" then return demos
  else return main end
end

-- The current screen's view with the remembered cursor applied. Both on_key and
-- on_ui go through this so they always agree on the selection.
local function current()
  local s = scene()
  if s ~= active then cursor[s] = 1; active = s end
  local v = view_for(s)
  v.cursor = cursor[s] or 1
  return v, s
end

function on_key(key)
  local v, s = current()
  if key == "up" then
    v:nav(-1)
  elseif key == "down" then
    v:nav(1)
  elseif key == "return" or key == "keypad enter" then
    v:activate()   -- may switch scenes; cursor state for the new one resets above
    return
  else
    return
  end
  cursor[s] = v.cursor
end

function on_ui()
  local v = current()
  v:render()
  skeleton()   -- the bobbing fire-skeleton appears on every menu screen
end
