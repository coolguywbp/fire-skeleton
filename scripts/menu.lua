-- scripts/menu.lua
--
-- The main menu and options screen, drawn with the immediate-mode `ui` toolkit.
--
-- Formatting is CSS-like: every widget takes an opts table of layout/style
-- properties. Reusable "styles" are just tables (THEME below), and style(a, b)
-- merges them with a cascade (later overrides earlier), e.g.
--   ui.label("OPTIONS", style(THEME.title, { size = 90 }))
--
-- Layout properties:  dir="row"|"column", gap, pad (number | {t,r,b,l} |
--   {top=,right=,bottom=,left=}), align_x/align_y, width/height ("grow"|"fit"|
--   number|"NN%"), color, radius, border={width=,color=}.
-- Text properties:    size, color, font, line (line height).

local THEME = {
  -- Full-screen black backdrop; content padded like the original menu.
  screen = { width = "grow", height = "grow", dir = "column",
             pad = { top = 200, bottom = 200, left = 100, right = 100 },
             gap = 164, color = { 0, 0, 0, 255 } },
  -- The vertical stack of menu entries.
  list   = { dir = "column", gap = 16, pad = 16 },
  title  = { size = 130, line = 120, font = 1 },
  item   = { size = 54 },
}

local function skeleton()
  local off = math.sin(time() * 2.0) * 50
  local w = 360
  local h = w * 644 / 450
  ui.image(0, SCREEN_W - w - 60, 180 + off, w, h)
end

local function main_menu()
  ui.panel(THEME.screen, function()
    ui.label("FIRE SKELETON INVADER", THEME.title)
    ui.panel(THEME.list, function()
      if ui.button("play",      "PLAY",      THEME.item) then goto_scene("play")      end
      if ui.button("benchmark", "BENCHMARK", THEME.item) then goto_scene("benchmark") end
      if ui.button("options",   "OPTIONS",   THEME.item) then goto_scene("options")   end
      if ui.button("exit",      "EXIT",      THEME.item) then quit()                  end
    end)
  end)
  skeleton()
end

local function options_menu()
  ui.panel(THEME.screen, function()
    ui.label("OPTIONS", style(THEME.title, { size = 90 }))
    ui.panel(THEME.list, function()
      ui.button("audio", "AUDIO", THEME.item)   -- placeholders for now
      ui.button("video", "VIDEO", THEME.item)
      if ui.button("back", "BACK", THEME.item) then goto_scene("menu") end
    end)
  end)
end

function on_ui()
  if scene() == "options" then options_menu() else main_menu() end
end
