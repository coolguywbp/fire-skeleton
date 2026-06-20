-- scripts/cube.lua
--
-- A rotating 3D cube. The 3D maths and rendering live entirely in the C `g3d`
-- module; this scene only animates the rotation angles and emits the cube
-- primitive each frame. on_ui() runs inside the layout pass; g3d buffers the
-- projected lines and the engine draws them in the render pass.

function on_ui()
  local t = time()
  g3d.cube(0, 0, 0, 1.6, {
    rx = t * 0.7,
    ry = t * 1.0,
    rz = t * 0.3,
    fill = true,                      -- shaded solid faces (g3d does the shading)
    color = { 235, 235, 245, 255 },   -- base colour; faces are Lambert-shaded
    width = 2,                        -- thin wireframe edges on top
  })
end
