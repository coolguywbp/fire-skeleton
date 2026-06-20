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

-- Safety rails. The spawn rate scales with dt so the ramp is frame-rate
-- independent, but that creates a feedback loop: as load slows the frame down,
-- dt grows and we'd spawn MORE per frame -> even slower -> runaway. On a single
-- threaded WebGL (web) build that can spawn hundreds of thousands of sprites in
-- a few frames and hang the GPU. So: clamp the dt used for spawning, cap the
-- per-frame batch, and enforce a hard ceiling on the live count. The FPS floor
-- still ends the ramp early in practice; these just guarantee it can't explode.
local DT_CLAMP     = 0.05                      -- never treat a frame as >50ms
local MAX_BATCH    = 100                       -- max spawned/despawned per frame
local MAX_N        = IS_WEB and 3000 or 100000 -- hard ceiling on live entities

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
local warming   -- skip the first sample window after a reset (load-frame hitch)

local function reset()
  if n and n > 0 then despawn("Bench", n) end
  phase, n, peak, prev_n = "up", 0, 0, 0
  accum_t, accum_f, done_t = 0, 0, 0
  warming = true
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
  -- would measure spawn cost instead of steady-state simulation). dt is clamped
  -- and the batch capped so a slow frame can never trigger a runaway spawn.
  local rate = math.floor(SPAWN_RATE * math.min(dt, DT_CLAMP)) + 1
  if rate > MAX_BATCH then rate = MAX_BATCH end
  if phase == "up" then
    local room = MAX_N - n
    if room < rate then rate = room end
    if rate > 0 then
      spawn_many("Bench", rate)
      n = n + rate
    end
    -- Hit the hard ceiling: treat it as the peak and start ramping down so the
    -- web build can't keep piling on sprites past a safe limit.
    if n >= MAX_N then
      peak  = (prev_n > 0) and prev_n or n
      phase = "down"
      log(string.format("Benchmark hit cap: %d entities", n))
    end
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

  -- The first window after a reset spans the scene-load frame (script parse +
  -- on_start spawn), whose dt is abnormally large and would read as a near-zero
  -- FPS -- enough to trip the floor and report a bogus instant "peak". Discard
  -- that first sample and start measuring from a clean window.
  if warming then warming = false; return end

  if phase == "up" then
    hud(string.format("BENCHMARK: ramping up... %d  (%d fps)", n, math.floor(smoothed)))
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
