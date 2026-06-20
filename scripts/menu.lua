-- scripts/menu.lua
--
-- The main menu and options screen, drawn with the immediate-mode `ui` toolkit.
-- This reproduces the original C/Clay menu exactly (same paddings, gaps, fixed
-- 300px list, growing buttons, left-aligned text, bobbing skeleton).
--
-- Formatting is CSS-like: every widget takes an opts table; reusable "styles"
-- are just tables (THEME), and style(a, b) merges them with a cascade.
-- Properties: dir, gap, pad (num | {t,r,b,l} | {top=,..}), align_x/align_y,
--   width/height ("grow"|"fit"|num|"NN%"), color, radius, border; text: size,
--   color, font, line; button also: align, width, height.

local THEME = {
  -- Full-screen black backdrop; content padded like the original.
  screen   = { width = "grow", height = "grow", dir = "column",
               pad = { top = 200, bottom = 200, left = 100, right = 100 },
               gap = 164, color = { 0, 0, 0, 255 } },
  -- Title sits in a growing box at the top.
  titlebox = { width = "grow", height = "grow" },
  title    = { size = 130, line = 120, font = 1 },
  -- The 300px-wide list; buttons grow to fill its height, text left-aligned.
  list     = { width = 300, height = "grow", dir = "column", gap = 16, pad = 16 },
  item     = { size = 54, height = "grow", align = "left" },
}

local function skeleton()
  -- Bobbing fire-skeleton (image 0), 450px wide, aspect 450/644, anchored to
  -- the right like the original (right edge 100px from the screen edge).
  local off = math.sin(time() * 2.0) * 50
  local w, h = 450, 644
  ui.image(0, SCREEN_W - w - 100, 150 + off, w, h)
end

local function main_menu()
  ui.panel(THEME.screen, function()
    ui.panel(THEME.titlebox, function()
      ui.label("FIRE SKELETON INVADER", THEME.title)
    end)
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
    ui.panel(THEME.titlebox, function()
      ui.label("OPTIONS", style(THEME.title, { size = 90 }))
    end)
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
