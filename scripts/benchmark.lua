-- scripts/benchmark.lua
--
-- Adaptive ECS stress benchmark (ported from C to Lua). It ramps the entity
-- count up until the frame rate hits a floor (recording the peak), ramps back
-- down to a single object, pauses, then loops.
--
-- Only the *policy* (how many to spawn/despawn this frame) lives here. The heavy
-- per-frame work -- moving and bouncing every sprite -- stays in the C
-- VelocitySystem, so the measured throughput reflects the engine, not Lua.

local MIN_FPS      = 30.0     -- frame-rate floor we ramp up to
local TARGET       = 1        -- settle count after the peak
local SAMPLE_SEC   = 0.4      -- FPS averaging window
local SPAWN_RATE   = 1500.0   -- entities added/removed per second (frame-spread)
local RESTART_WAIT = 1.5      -- pause at the floor before looping

-- No Collision component: these sprites are pure load and must not feed the
-- collision system. Empty Transform/Velocity = the C constructors' random
-- position and velocity.
prefab "Bench" {
  Transform = {},
  Velocity  = {},
  Sprite    = { image = "skeleton" },
}

local phase     -- "up" | "down" | "done"
local n         -- entities alive
local peak
local prev_n
local accum_t, accum_f
local done_t

local function reset()
  if n and n > 0 then despawn("Bench", n) end
  phase, n, peak, prev_n = "up", 0, 0, 0
  accum_t, accum_f, done_t = 0, 0, 0
  spawn_many("Bench", 1)   -- start with a single object
  n = 1
end

function on_start()
  reset()
end

function on_update(dt)
  if phase == "done" then
    done_t = done_t + dt
    if done_t >= RESTART_WAIT then reset() end
    return
  end

  -- Per-frame path is kept allocation-free (no string building): spread
  -- spawning/despawning evenly across frames so no single frame bursts (which
  -- would measure spawn cost instead of steady-state simulation).
  local rate = math.floor(SPAWN_RATE * dt) + 1
  if phase == "up" then
    spawn_many("Bench", rate)
    n = n + rate
  elseif phase == "down" and n > TARGET then
    n = n - despawn("Bench", math.min(rate, n - TARGET))
  end

  -- FPS sampling window for a stable reading. The HUD string is only built here
  -- (~2-3 times/sec), never per frame, so it doesn't churn the GC under load.
  accum_t = accum_t + dt
  accum_f = accum_f + 1
  if accum_t < SAMPLE_SEC then return end
  local smoothed = accum_f / accum_t
  accum_t, accum_f = 0, 0

  if phase == "up" then
    hud("BENCHMARK: ramping up... " .. n)
    if smoothed <= MIN_FPS then
      -- prev_n held the floor at the previous sample; report it as the peak
      -- (avoids within-window overshoot).
      peak  = (prev_n > 0) and prev_n or n
      phase = "down"
      log(string.format("Benchmark peak: %d entities >= %d fps", peak, MIN_FPS))
    else
      prev_n = n
    end
  elseif phase == "down" then
    hud(string.format("BENCHMARK: peak %d @%dfps, settling (%d)", peak, MIN_FPS, n))
    if n <= TARGET then
      phase = "done"
      hud(string.format("BENCHMARK: peak %d entities @%dfps", peak, MIN_FPS))
      log(string.format("Benchmark done. Settled at %d (~%.0f fps)", n, smoothed))
    end
  end
end
