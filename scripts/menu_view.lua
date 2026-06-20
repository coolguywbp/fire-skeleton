-- scripts/menu_view.lua
--
-- Layout + styles for the menu scene, kept together (the "HTML + CSS").
-- The scene (scripts/menu.lua) imports this, mounts a view per screen and
-- binds button ids to actions. Structure/look here; behavior there.
--
-- A node: { tag = "panel"|"list"|"label"|"button"|"image"|"text"|"rect",
--           class = "<style>", id = "<button id>", text = "...",
--           children = { ... } }. `class` looks up the styles table below.

-- Styles (the "CSS"): class name -> properties. Shared by both screens.
local styles = {
  -- Full-screen black backdrop; content padded like the original.
  screen   = { width = "grow", height = "grow", dir = "column",
               pad = { top = 200, bottom = 200, left = 100, right = 100 },
               gap = 164, color = { 0, 0, 0, 255 } },
  -- Title sits in a growing box at the top.
  titlebox = { width = "grow", height = "grow" },
  title    = { size = 130, line = 120, font = 1 },
  optitle  = { size = 90,  line = 120, font = 1 },
  -- The 300px-wide list; buttons grow to fill its height, text left-aligned.
  list     = { width = 300, height = "grow", dir = "column", gap = 0, pad = 0 },
  item     = { size = 54, height = "grow", align = "left" },
}

return {
  -- Main menu screen.
  menu = {
    styles = styles,
    tree = {
      { tag = "panel", class = "screen", children = {
        { tag = "panel", class = "titlebox", children = {
          { tag = "label", class = "title", text = "FIRE SKELETON" },
        } },
        { tag = "list", class = "list", children = {
          { tag = "button", id = "play",      class = "item", text = "PLAY" },
          { tag = "button", id = "options",   class = "item", text = "OPTIONS" },
          { tag = "button", id = "exit",      class = "item", text = "EXIT" },
        } },
      } },
    },
  },

  -- Demo-picker screen (reached from PLAY).
  demos = {
    styles = styles,
    tree = {
      { tag = "panel", class = "screen", children = {
        { tag = "panel", class = "titlebox", children = {
          { tag = "label", class = "optitle", text = "DEMOS" },
        } },
        { tag = "list", class = "list", children = {
          { tag = "button", id = "invaders",  class = "item", text = "INVADERS" },
          { tag = "button", id = "slots",     class = "item", text = "SLOTS" },
          { tag = "button", id = "benchmark", class = "item", text = "BENCHMARK" },
          { tag = "button", id = "back",      class = "item", text = "BACK" },
        } },
      } },
    },
  },

  -- Options screen.
  options = {
    styles = styles,
    tree = {
      { tag = "panel", class = "screen", children = {
        { tag = "panel", class = "titlebox", children = {
          { tag = "label", class = "optitle", text = "OPTIONS" },
        } },
        { tag = "list", class = "list", children = {
          { tag = "button", id = "audio", class = "item", text = "AUDIO" },
          { tag = "button", id = "video", class = "item", text = "VIDEO" },
          { tag = "button", id = "back",  class = "item", text = "BACK" },
        } },
      } },
    },
  },
}
