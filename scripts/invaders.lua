-- scripts/invaders.lua
--
-- Stage 2 placeholder: just enough to prove the Play mode loads its own script
-- (a movable player). The full Space Invaders clone arrives in Stage 3.

prefab "Player" {
  Transform = { w = 64, h = 64 },
  Sprite    = { image = "skeleton" },
  Collision = {},
}

local player
local px, py
local SPEED = 16

function on_start()
  px = SCREEN_W / 2 - 32
  py = SCREEN_H - 110
  player = spawn_at("Player", px, py)
  hud("PLAY (work in progress) -- arrows to move")
end

function on_key(key)
  if key == "left"  then px = px - SPEED end
  if key == "right" then px = px + SPEED end
  if px < 0 then px = 0 end
  if px > SCREEN_W - 64 then px = SCREEN_W - 64 end
  set_pos(player, px, py)
end
