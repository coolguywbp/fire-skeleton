-- scripts/slots.lua
--
-- A 3x3 slot-machine minigame built entirely on the Lua scripting layer and the
-- immediate-mode `ui` toolkit (no ECS entities -- everything is drawn each frame
-- in on_ui). Each of the three columns is an independent "reel": a random strip
-- of symbols that scrolls vertically and decelerates to a stop, staggered so the
-- reels settle left-to-right. When all three stop we score the paylines.
--
-- Controls: Space (or the SPIN button) to spin. Esc returns to the menu.

-- Symbols -------------------------------------------------------------------
-- Each symbol is a single glyph drawn in its own colour, plus a payout factor
-- (multiplied by the bet, per winning payline).
local SYMS = {
  { ch = "7", color = { 235, 200,  60, 255 }, pay = 25 },
  { ch = "$", color = { 120, 220, 120, 255 }, pay = 15 },
  { ch = "A", color = { 235,  90,  90, 255 }, pay = 10 },
  { ch = "K", color = { 110, 160, 240, 255 }, pay =  6 },
  { ch = "Q", color = { 205, 120, 230, 255 }, pay =  4 },
  { ch = "J", color = { 120, 210, 220, 255 }, pay =  3 },
}
local NS = #SYMS

-- Tuning --------------------------------------------------------------------
local STRIP_LEN = 24       -- symbols on each reel's random strip
local SPEED     = 16       -- scroll speed, symbols/second
local STOP_AT   = { 0.9, 1.35, 1.8 }  -- per-reel stop time (staggered)
local START_CREDITS = 100
local BET           = 5

-- Paylines: which row (1..3) each of the three columns contributes. Three
-- horizontals plus the two diagonals -- the classic 3x3 layout.
local PAYLINES = {
  { 1, 1, 1 }, { 2, 2, 2 }, { 3, 3, 3 },   -- rows
  { 1, 2, 3 }, { 3, 2, 1 },                -- diagonals
}

-- Geometry (centred grid on the 1280x960 window).
local CELL, GAP = 150, 18
local GRID = 3 * CELL + 2 * GAP
local OX = (SCREEN_W - GRID) / 2
local OY = 190

-- State ---------------------------------------------------------------------
local reels       -- [c] = { strip, pos, spinning, t, base }
local spinning    -- any reel still moving?
local credits, message
local win_cells   -- set of "col,row" keys highlighted after a win

-- The visible symbol index at (col, row), 1-based. While a reel spins we read
-- the scrolling position; once stopped we read its locked base offset.
local function sym(col, row)
  local r = reels[col]
  local b = r.spinning and (math.floor(r.pos) % STRIP_LEN) or r.base
  return r.strip[(b + (row - 1)) % STRIP_LEN + 1]
end

local function evaluate()
  local total = 0
  win_cells = {}
  for _, line in ipairs(PAYLINES) do
    local s = sym(1, line[1])
    if s == sym(2, line[2]) and s == sym(3, line[3]) then
      total = total + SYMS[s].pay * BET
      for col = 1, 3 do win_cells[col .. "," .. line[col]] = true end
    end
  end
  credits = credits + total
  message = total > 0 and ("WIN  +" .. total) or "NO WIN"
end

local function spin()
  if spinning or credits < BET then return end
  credits = credits - BET
  spinning, message, win_cells = true, "", {}
  for c = 1, 3 do
    local strip = {}
    for i = 1, STRIP_LEN do strip[i] = math.random(NS) end
    reels[c] = { strip = strip, pos = 0, spinning = true, t = 0, base = 0 }
  end
end

-- Callbacks -----------------------------------------------------------------
function on_start()
  math.randomseed(math.floor(time() * 1000) + 1)
  reels, spinning, win_cells = {}, false, {}
  credits = START_CREDITS
  message = "PRESS  SPACE  TO  SPIN"
  for c = 1, 3 do
    local strip = {}
    for i = 1, STRIP_LEN do strip[i] = math.random(NS) end
    reels[c] = { strip = strip, pos = 0, spinning = false, t = 0,
                 base = math.random(STRIP_LEN) - 1 }
  end
end

function on_key(key)
  if key == "space" then spin() end
end

function on_update(dt)
  if not spinning then return end
  for c = 1, 3 do
    local r = reels[c]
    if r.spinning then
      r.t   = r.t + dt
      r.pos = r.pos + SPEED * dt
      if r.t >= STOP_AT[c] then
        r.spinning = false
        r.base = math.floor(r.pos + 0.5) % STRIP_LEN  -- land on a clean row
      end
    end
  end
  local still = false
  for c = 1, 3 do if reels[c].spinning then still = true end end
  if not still then spinning = false; evaluate() end
end

-- UI ------------------------------------------------------------------------
local function draw_cell(col, row)
  local x = OX + (col - 1) * (CELL + GAP)
  local y = OY + (row - 1) * (CELL + GAP)
  local won = win_cells["" .. col .. "," .. row]
  ui.rect(x, y, CELL, CELL,
          { color = won and { 60, 70, 40, 255 } or { 22, 22, 30, 255 }, radius = 12 })

  local s   = SYMS[sym(col, row)]
  local size = 100
  -- Rough centring for a single glyph (the font is ~0.6em wide).
  ui.text(x + (CELL - size * 0.55) / 2, y + (CELL - size) / 2 + 6, s.ch,
          { size = size, font = 1, color = s.color })
end

function on_ui()
  ui.text(SCREEN_W / 2 - 110, 60, "SLOTS", { size = 80, font = 1 })

  ui.rect(OX - 16, OY - 16, GRID + 32, GRID + 32,
          { color = { 40, 40, 60, 255 }, radius = 16 })
  for col = 1, 3 do
    for row = 1, 3 do draw_cell(col, row) end
  end

  local infoY = OY + GRID + 28
  ui.text(OX, infoY, "CREDITS  " .. credits, { size = 34 })
  ui.text(OX + GRID - 130, infoY, "BET  " .. BET, { size = 34 })

  local msgColor = (message:sub(1, 3) == "WIN") and { 137, 220, 160, 255 }
                                                 or { 220, 220, 220, 255 }
  ui.text(SCREEN_W / 2 - 200, infoY + 50, message, { size = 36, color = msgColor })

  local bw, bh = 260, 72
  local bx, by = SCREEN_W / 2 - bw / 2, infoY + 110
  local can = not spinning and credits >= BET
  if ui.button("spin", bx, by, bw, bh, spinning and "SPINNING" or "SPIN",
               { size = 40, color = can and { 60, 110, 70, 255 } or { 50, 50, 60, 255 },
                 hover_color = { 80, 150, 95, 255 } }) then
    spin()
  end

  if credits < BET and not spinning then
    ui.text(SCREEN_W / 2 - 160, by + bh + 20, "OUT OF CREDITS  (Esc)",
            { size = 28, color = { 240, 120, 120, 255 } })
  end
end
