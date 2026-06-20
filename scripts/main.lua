-- scripts/main.lua
--
-- Phase 4: event hooks.
--   on_collision(a, b) -- C detects overlapping CollisionComponent entities
--                         (spatial-hash broad-phase) and calls this
--   on_key(key)        -- a key press in the level (lowercased SDL name)
--
-- Adding `Collision = {}` to a prefab opts its entities into collision; the
-- benchmark's bouncing sprites have no CollisionComponent, so they are ignored.

prefab "Skeleton" {
  Transform = { w = 64, h = 64 },
  Velocity  = { vy = 2.0 },
  Sprite    = { image = "skeleton" },
  Collision = {},
}

function on_start()
  -- Two overlapping skeletons to demonstrate collision detection on entry.
  spawn_at("Skeleton", 400, 300)
  spawn_at("Skeleton", 420, 300)
  -- A timed wave of falling skeletons.
  start(skeleton_wave)
end

function skeleton_wave()
  for i = 1, 5 do
    spawn_at("Skeleton", 200 + i * 80, -20)
    wait(0.5)
  end
end

function on_collision(a, b)
  log(string.format("collision: %d <-> %d", a, b))
  destroy(b) -- like a bullet hit: remove one of the pair
end

function on_key(key)
  log("key pressed: " .. key)
  if key == "space" then
    spawn_at("Skeleton", 600, 100)
  end
end

function on_update(dt)
  -- game-level per-frame logic goes here
end
