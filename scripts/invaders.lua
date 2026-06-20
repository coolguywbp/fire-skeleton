-- scripts/invaders.lua
--
-- A small Space Invaders clone built entirely on the Lua scripting layer.
-- Movement is authored in Lua and synced to the ECS via set_pos(); the C side
-- handles rendering (SpriteRenderSystem) and collision detection (spatial hash
-- over CollisionComponent), calling back into on_collision(). The HUD and the
-- game-over screen are drawn with the immediate-mode `ui` toolkit in on_ui().
--
-- Controls: Left/Right or A/D to move, Space to shoot (hold to autofire).

-- Tuning ---------------------------------------------------------------------
local IW, IH       = 48, 48        -- invader size
local COLS, ROWS   = 8, 4
local GAP_X, GAP_Y = 36, 28        -- spacing between invaders
local MARGIN_X     = 120
local MARGIN_Y     = 80

local PLAYER_W     = 64
local PLAYER_SPEED = 520           -- px/sec (dt-scaled; the level is uncapped)
local BULLET_SPEED = 760           -- px/sec, upward
local BOMB_SPEED   = 330           -- px/sec, downward
local INV_SPEED    = 55            -- px/sec, horizontal
local INV_DROP     = 26            -- px dropped when the formation reverses
local SHOOT_CD     = 0.35          -- seconds between player shots
local BOMB_EVERY   = 1.1           -- seconds between enemy shots

prefab "Player"  { Transform = { w = PLAYER_W, h = 64 }, Sprite = { image = "skeleton" }, Collision = {} }
prefab "Invader" { Transform = { w = IW, h = IH },       Sprite = { image = "skeleton" }, Collision = {} }
prefab "Bullet"  { Transform = { w = 10, h = 22 },       Sprite = { image = "skeleton" }, Collision = {} }
prefab "Bomb"    { Transform = { w = 10, h = 22 },       Sprite = { image = "skeleton" }, Collision = {} }

-- State ----------------------------------------------------------------------
local player, px, py
local invaders, bullets, bombs     -- keyed by entity id
local fx, fy, dir                  -- formation offset + direction
local shoot_cd, bomb_t
local score, lives, state          -- state: "play" | "over"
local over_reason                  -- "dead" | "wave"

local function destroy_all(t)
  for id in pairs(t) do destroy(id) end
end

local function spawn_wave()
  fx, fy, dir = 0, 0, 1
  for r = 0, ROWS - 1 do
    for c = 0, COLS - 1 do
      local bx = MARGIN_X + c * (IW + GAP_X)
      local by = MARGIN_Y + r * (IH + GAP_Y)
      local id = spawn_at("Invader", bx, by)
      invaders[id] = { bx = bx, by = by }
    end
  end
end

local function new_game()
  if player then destroy(player) end
  if invaders then destroy_all(invaders) end
  if bullets then destroy_all(bullets) end
  if bombs then destroy_all(bombs) end

  invaders, bullets, bombs = {}, {}, {}
  score, lives, state, over_reason = 0, 3, "play", nil
  shoot_cd, bomb_t = 0, 0
  px = SCREEN_W / 2 - PLAYER_W / 2
  py = SCREEN_H - 110
  player = spawn_at("Player", px, py)
  spawn_wave()
end

local function next_wave()
  destroy_all(bullets); destroy_all(bombs)
  bullets, bombs = {}, {}
  spawn_wave()
  state = "play"
end

local function game_over()
  state, over_reason = "over", "dead"
end

local function player_hit()
  lives = lives - 1
  if lives <= 0 then game_over() end
end

local function resolve_hit(bullet_id, invader_id)
  destroy(bullet_id);  bullets[bullet_id]   = nil
  destroy(invader_id); invaders[invader_id] = nil
  score = score + 10
end

local function drop_bomb()
  local ids = {}
  for id in pairs(invaders) do ids[#ids + 1] = id end
  if #ids == 0 then return end
  local v = invaders[ids[math.random(#ids)]]
  local x, y = v.bx + fx + IW / 2, v.by + fy + IH
  local id = spawn_at("Bomb", x, y)
  bombs[id] = { x = x, y = y }
end

-- Callbacks ------------------------------------------------------------------
function on_start()
  new_game()
end

function on_update(dt)
  if state ~= "play" then return end

  -- Player (held-key movement): arrow keys or A/D.
  if key_down("left")  or key_down("a") then px = px - PLAYER_SPEED * dt end
  if key_down("right") or key_down("d") then px = px + PLAYER_SPEED * dt end
  if px < 0 then px = 0 end
  if px > SCREEN_W - PLAYER_W then px = SCREEN_W - PLAYER_W end
  set_pos(player, px, py)

  -- Shooting: hold Space to autofire on a cooldown. Driven from the live key
  -- state (not on_key) so it can't be interrupted when another key's OS repeat
  -- takes over the repeat stream while Space stays held.
  if shoot_cd > 0 then shoot_cd = shoot_cd - dt end
  if key_down("space") and shoot_cd <= 0 then
    local bx, by = px + PLAYER_W / 2 - 5, py - 28
    local id = spawn_at("Bullet", bx, by)
    bullets[id] = { x = bx, y = by }
    shoot_cd = SHOOT_CD
  end

  -- Player bullets fly up; despawn off the top.
  for id, b in pairs(bullets) do
    b.y = b.y - BULLET_SPEED * dt
    if b.y < -30 then destroy(id); bullets[id] = nil
    else set_pos(id, b.x, b.y) end
  end

  -- Invader formation: move together; reverse + drop at the screen edges.
  local minx, maxx, any = math.huge, -math.huge, false
  for _, v in pairs(invaders) do
    any = true
    local x = v.bx + fx
    if x < minx then minx = x end
    if x + IW > maxx then maxx = x + IW end
  end

  if not any then
    -- Wave cleared: brief pause, then a fresh wave (score carries over).
    state, over_reason = "over", "wave"
    start(function() wait(1.5); next_wave() end)
    return
  end

  local delta = INV_SPEED * dir * dt
  if (maxx + delta) > SCREEN_W or (minx + delta) < 0 then
    dir = -dir
    fy = fy + INV_DROP
  else
    fx = fx + delta
  end

  local reached = false
  for id, v in pairs(invaders) do
    local y = v.by + fy
    set_pos(id, v.bx + fx, y)
    if y + IH >= py then reached = true end  -- invaders reached the player line
  end
  if reached then game_over(); return end

  -- Enemy fire.
  bomb_t = bomb_t + dt
  if bomb_t >= BOMB_EVERY then bomb_t = 0; drop_bomb() end

  -- Bombs fall; despawn off the bottom.
  for id, b in pairs(bombs) do
    b.y = b.y + BOMB_SPEED * dt
    if b.y > SCREEN_H + 30 then destroy(id); bombs[id] = nil
    else set_pos(id, b.x, b.y) end
  end
end

function on_collision(a, b)
  -- Player bullet hits an invader.
  if bullets[a] and invaders[b] then resolve_hit(a, b); return end
  if bullets[b] and invaders[a] then resolve_hit(b, a); return end

  -- Enemy bomb hits the player.
  if (bombs[a] and b == player) or (bombs[b] and a == player) then
    local bid = bombs[a] and a or b
    destroy(bid); bombs[bid] = nil
    player_hit()
  end
end

-- UI (immediate-mode, drawn every frame) -------------------------------------
function on_ui()
  ui.text(24, 16, "SCORE  " .. score, { size = 30 })
  ui.text(SCREEN_W - 190, 16, "LIVES  " .. lives, { size = 30 })

  if state ~= "over" then return end

  if over_reason == "wave" then
    ui.text(SCREEN_W / 2 - 150, SCREEN_H / 2 - 30, "WAVE CLEAR!",
            { size = 56, color = { 137, 220, 160, 255 } })
    return
  end

  -- Game over panel + restart button.
  ui.rect(SCREEN_W / 2 - 230, SCREEN_H / 2 - 130, 460, 260,
          { color = { 10, 10, 20, 215 }, radius = 14 })
  ui.text(SCREEN_W / 2 - 150, SCREEN_H / 2 - 100, "GAME OVER",
          { size = 56, color = { 240, 90, 90, 255 } })
  ui.text(SCREEN_W / 2 - 70, SCREEN_H / 2 - 26, "Score: " .. score, { size = 30 })
  if ui.button("restart", SCREEN_W / 2 - 110, SCREEN_H / 2 + 36, 220, 64, "RESTART",
               { size = 36 }) then
    new_game()
  end
end
