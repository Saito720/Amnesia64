# Uncapped rendering with fixed simulation

`Engine/LimitFPS=false` now renders interpolated poses between simulation updates.
Leave `cEngineInitVars::mGame.mlUpdateRate` at **60**. The existing capped option
still renders the current fixed pose; disabling VSync allows rendering above the
display refresh rate.

## Timing and state ownership

- A monotonic, high-resolution clock accumulates elapsed time. Every gameplay,
  physics, animation-event and force update retains the existing `1/60` second
  timestep. Slow motion and fast forward change tick frequency, not step size.
- Before each complete update, the scene captures previous local transforms and
  camera state. Rendering uses the remaining fraction of a timestep to blend
  previous/current poses. A stalled frame runs at most the configured catch-up
  count (six by default) before dropping whole overdue ticks.
- `GetWorldMatrix()` and physics state remain authoritative. Renderer-facing
  `GetRenderWorldMatrix()` and `GetRenderBoundingVolume()` supply separate cached
  poses and conservative bounds. Rendering never calls physics transform setters
  or entity transform callbacks to install an interpolated pose.
- Local transforms blend translation, shortest-path quaternion rotation and
  scale before composing the hierarchy. Bone poses are interpolated before
  skinning; shadow, culling, lighting, billboard, beam, fog and outline paths use
  the same presentation state.
- Particles and ropes have separate presentation history. Verlet's previous
  integration position is never repurposed for rendering. Camera projection
  changes also interpolate. Teleports, pause/resume and inactive worlds settle
  history; continuous ladder and ledge motion retains interpolation.

Interpolation intentionally presents the scene one fixed tick (about 16.7 ms)
behind simulation. Input and gameplay still update at 60 Hz; this change does not
add a separate mouse-input prediction path. Discrete events, spawning/destruction,
texture animation frames and GUI state changes retain their existing update
semantics. A custom teleport should call `ResetRenderInterpolation()` after
setting the entity pose, or `ResetInterpolation()` for a camera. Continuous
character motion can use `SetPosition(position, smooth, false)` to preserve its
presentation history without changing positional smoothing.

## Automated validation

From the repository root, in PowerShell with Visual Studio C++ tools installed:

```powershell
./HPL2/tests/UncappedFPS/RunTests.ps1
```

This builds the x64 Release game and links console tests against the real HPL2
and Newton libraries. Output goes to the ignored `bld/uncapped-fps-tests` folder.
Use `-SkipBuild` after an existing build, or `-Test 'PhysicsCadence*'` to select a
suite. Assertions remain active in Release builds.

- Timer: 30/60/120/144/240/1000/4000 FPS, sub-millisecond precision, irregular
  frames, stalls, reset, zero speed, slow motion and fast forward.
- Transforms: distinct substep poses, rotation/scale, hierarchy, bounds,
  creation/deletion, resets and unchanged authoritative transforms/callbacks.
- Camera: view/projection interpolation, shortest yaw wrap, attachment resets,
  endpoints and unchanged authoritative camera/frustum state.
- Newton: gravity, force, torque, contact and impulse trajectories at
  30/60/120/144/240/1000 FPS; identical fixed-step counts and final physics state;
  changing render poses during frames without a physics update. Also covers
  character teleport versus continuous scripted movement.
- Particles/ropes: birth/respawn, color/size/spin/tail interpolation and identical
  actual Verlet trajectories with different numbers of render samples.

An optional OpenGL integration check uses the installed game's shader assets:

```powershell
./HPL2/tests/UncappedFPS/SmokeTest.ps1 -AssetsDirectory '<game installation directory>'
```

Build x64 Release first. This runs the real deferred renderer with a procedural
orange cube attached to a Newton body. After one fixed physics tick, samples at
alpha 0.25 and 0.75 must produce different visible pixels; repeating alpha 0.25
must reproduce the same pixels, with no change to authoritative simulation
state. Logs and PPM previews go to `bld/uncapped-fps-render-smoke`. It uses a hidden
window, does not initialize game profiles/saves, and disables asset cache saving.

## In-game checks

Compare uncapped rendering at high refresh against the capped option: walk and
turn beside doorframes, carry and throw objects, operate doors/levers/wheels,
watch enemies and ragdolls, climb ladders/ledges, inspect lantern shadows and
particle/rope motion, then pause, load a save and teleport. Include a GPU-limited
run below 60 FPS. Automated checks establish timing and state isolation; these
checks are still needed to assess complete maps, interactions and visual quality.
