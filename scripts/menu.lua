-- scripts/menu.lua
--
-- The menu scene: behavior only. Structure and styling live in the imported
-- layout file (scripts/menu_view.lua); here we mount each screen and bind its
-- buttons to actions. mount()/view:on()/view:render() come from the engine
-- prelude (the declarative view runtime).

local L = require("menu_view")

local main = mount(L.menu)
  :on("play",      function() goto_scene("play")      end)
  :on("benchmark", function() goto_scene("benchmark") end)
  :on("options",   function() goto_scene("options")   end)
  :on("exit",      quit)

local opts = mount(L.options)
  :on("back", function() goto_scene("menu") end)
  -- audio / video are placeholders for now (no handler bound yet).

-- The bobbing fire-skeleton is animated (driven by time()), so it stays in the
-- scene rather than the static layout: image 0, 450x644, anchored to the right
-- (right edge 100px from the screen edge).
local function skeleton()
  local off = math.sin(time() * 2.0) * 50
  local w, h = 450, 644
  ui.image(0, SCREEN_W - w - 100, 150 + off, w, h)
end

function on_ui()
  if scene() == "options" then
    opts:render()
  else
    main:render()
    skeleton()
  end
end
