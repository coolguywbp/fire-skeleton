-- scripts/cards.lua
--
-- Cards demo, phase 1: three cards lie tilted back on a "table" and, when the
-- pointer hovers one, it lifts toward the camera and tilts toward the cursor --
-- the Balatro/Steam parallax. All the 3D maths and rasterisation live in C
-- (the g3d module); this scene only lays the cards out, detects hover, and
-- animates the lift/tilt it hands to g3d.card() each frame. Materials
-- (holographic / chrome / glass shaders) come in a later phase; for now each
-- card is a flat colour so the motion can be judged on its own.

local CARDS = {
  { name = "AKROMA",     material = "holo",   image = "akroma"     },
  { name = "LORD OF THE PIT", material = "chrome", image = "lordofpit" },
  { name = "NICOL BOLAS", material = "glass",  image = "nicolbolas" },
}

local CW, CH  = 0.85, 1.18   -- card size in world units (MTG aspect ~0.72)
local GAP     = 1.45         -- horizontal spacing between card centres
local BASE_Y  = -0.10        -- resting vertical position

local HOLD_TIME = 0.18       -- grace period after the pointer leaves a card

-- Per-card animation state.
local lift = { 0, 0, 0 }     -- smoothed lift (0 = resting, 1 = fully raised)
local hold = { 0, 0, 0 }     -- countdown that keeps a card up briefly after leave
local rotx = { 0, 0, 0 }     -- smoothed tilt angles (eased toward the cursor target
local roty = { 0, 0, 0 }     -- so the card turns like a physical object, not snaps)
-- Pose computed in on_update, consumed in on_ui.
local pose = {}

-- World x of card i, so the row is centred on the origin.
local function card_x(i)
  return (i - (#CARDS + 1) / 2) * GAP
end

-- Active pointer in logical coords: a touch wins over the mouse.
local function pointer()
  if touch_count() > 0 then
    local tx, ty = touch_pos(1)
    if tx then return tx, ty end
  end
  return mouse_pos()
end

-- Frame-rate-independent ease toward a target.
local function approach(cur, target, dt, rate)
  return cur + (target - cur) * (1 - math.exp(-rate * dt))
end

local function clamp(v, lo, hi)
  return v < lo and lo or (v > hi and hi or v)
end

function on_update(dt)
  if dt > 0.1 then dt = 0.1 end          -- ignore hitches (tab-out, first frame)
  local px, py = pointer()

  for i = 1, #CARDS do
    local x = card_x(i)
    -- Hover rect uses the card's CURRENT (raised) position/size, not its resting
    -- one: a lifted card grows, so a resting-sized rect would lose the pointer
    -- near the now-bigger card's edge and drop it too early. Sizing the rect by
    -- the live pose makes hover "sticky" -- it tracks the card you actually see.
    local lp = lift[i]
    local cy = BASE_Y + lp * 0.18
    local cz = -lp * 0.70
    local sx, sy, scale = g3d.project(x, cy, cz)
    local hw, hh = CW * 0.5 * scale, CH * 0.5 * scale
    local over = px >= sx - hw and px <= sx + hw and py >= sy - hh and py <= sy + hh

    -- Hover hysteresis: when the pointer leaves, keep the card raised for a short
    -- grace period instead of dropping at once -- it "waits" in case the mouse
    -- comes back, so brushing past or crossing a seam doesn't snap it down.
    if over then hold[i] = HOLD_TIME else hold[i] = math.max(0, hold[i] - dt) end
    local active = over or hold[i] > 0
    lift[i] = approach(lp, active and 1 or 0, dt, 12)

    -- Tilt target only while actually over the card (flat while just "waiting").
    local trx, try = 0, 0
    if over then
      local dx = clamp((px - sx) / hw, -1, 1)   -- pointer offset within the card
      local dy = clamp((py - sy) / hh, -1, 1)
      trx =  dy * 0.28   -- tilt AWAY from the cursor (inverted)
      try = -dx * 0.38
    end
    rotx[i] = approach(rotx[i], trx, dt, 14)     -- ease the tilt for a physical feel
    roty[i] = approach(roty[i], try, dt, 14)

    local l = lift[i]
    pose[i] = {
      x  = x,
      y  = BASE_Y + l * 0.18,   -- rise a touch
      z  = -l * 0.70,           -- move toward the camera (grows)
      rx = rotx[i] * l,         -- fade the tilt in/out with the lift
      ry = roty[i] * l,
    }
  end
end

function on_ui()
  g3d.camera(0, 0, -4)
  g3d.fov(55)

  ui.text(SCREEN_W / 2 - (#"CARDS" * 56 * 0.6) / 2, 40, "CARDS", { size = 56, font = 1 })

  -- No depth buffer: draw the least-raised card first so a hovered card that
  -- grows over its neighbours sits on top.
  local order = { 1, 2, 3 }
  table.sort(order, function(a, b) return lift[a] < lift[b] end)

  for _, i in ipairs(order) do
    local p = pose[i]
    if p then
      g3d.card(p.x, p.y, p.z, CW, CH,
        { material = CARDS[i].material, image = CARDS[i].image, rx = p.rx, ry = p.ry })
    end
  end
end
