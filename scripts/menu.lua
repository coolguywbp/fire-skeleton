-- scripts/menu.lua
--
-- The main menu and the options screen, drawn with the immediate-mode `ui`
-- toolkit. Buttons navigate the game via goto_scene()/quit(). The same script
-- handles both screens, branching on scene().

local function skeleton()
  -- The bobbing fire-skeleton image (image id 0), preserving its aspect ratio.
  local off = math.sin(time() * 2.0) * 50
  local w = 360
  local h = w * 644 / 450
  ui.image(0, SCREEN_W - w - 60, 180 + off, w, h)
end

local function menu_panel(build)
  ui.panel({ width = "grow", height = "grow", dir = "column",
             align_x = "center", align_y = "center", gap = 30,
             color = { 0, 0, 0, 255 } }, build)
end

local function main_menu()
  menu_panel(function()
    ui.label("FIRE SKELETON INVADER", { size = 120, font = 1 })
    if ui.button("play",      "PLAY")      then goto_scene("play")      end
    if ui.button("benchmark", "BENCHMARK") then goto_scene("benchmark") end
    if ui.button("options",   "OPTIONS")   then goto_scene("options")   end
    if ui.button("exit",      "EXIT")      then quit()                  end
  end)
  skeleton()
end

local function options_menu()
  menu_panel(function()
    ui.label("OPTIONS", { size = 90, font = 1 })
    ui.button("audio", "AUDIO")   -- placeholders for now
    ui.button("video", "VIDEO")
    if ui.button("back", "BACK") then goto_scene("menu") end
  end)
end

function on_ui()
  if scene() == "options" then options_menu() else main_menu() end
end
