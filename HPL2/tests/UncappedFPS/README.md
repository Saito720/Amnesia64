# Uncapped rendering with fixed simulation

`Engine/LimitFPS=false` now renders interpolated poses between simulation updates.
Leave `cEngineInitVars::mGame.mlUpdateRate` at **60**. The existing capped option
still renders the current fixed pose; disabling VSync allows rendering above the
display refresh rate.

The normal Graphics options expose **Uncap FPS**, applied with OK without a
restart and stored using the existing `Engine/LimitFPS` key. Fresh settings
enable both Uncap FPS and V-sync, including when the installed retail default
file contains its older choices. Existing saved preferences are preserved.

## Timing and state ownership

- A monotonic, high-resolution clock accumulates elapsed time. Every gameplay,
  physics, animation-event and force update retains the existing `1/60` second
  timestep. Slow motion and fast forward change tick frequency, not step size.
  Clock resets during an update preserve the current frame's catch-up budget.
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

Image trail keeps the original 60 Hz blend strength, with history retention
converted exponentially for actual elapsed render time. Its accumulation texture
uses RGB32F so small high-refresh blend weights do not round away and leave
persistent ghosts. This one texture costs approximately 24 MiB at 1080p instead
of 6 MiB, before driver padding. Drivers without float textures, or which reject
the float framebuffer, fall back through RGB16F to the original RGB8 format;
those legacy paths retain their lower-precision limitation. Existing resize,
effect-reset and reactivation paths clear the history as before. Other post
effects recompute from the current frame or advance on fixed simulation updates.

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
- Temporal effects: the frozen retail 60 Hz image-trail response versus equal
  elapsed time at 30–1000 FPS, irregular durations, stalls and invalid timing.

The multiplayer integration suite also exercises the real engine loop with
uncapped rendering, ownership corrections, remote player/lantern alignment,
stationary and moving skeleton caches, live settings, and map/menu/resize
transitions. See [its runner instructions](../../../tests/multiplayer/README.md).

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
It also sends black/white step inputs through the production image-trail shader,
reads the actual GPU pixels after 100 ms and one second at 30/60/144/240/1000 FPS,
and checks against the retail response within two 8-bit display levels. This
catches precision loss that coefficient-only tests cannot detect. Reactivation
and resized history must start immediately from the new input image.
The moving cube's actual pixels must also fit its render-bounds outline clip
rectangle; the fixture proves that clipping from the simulation bounds would
crop those pixels. Switching the real engine from capped to uncapped must render
the current scene without rewinding history or changing physics, while reapplying
the same setting must preserve ordinary interpolation.

## In-game checks

Compare uncapped rendering at high refresh against the capped option: walk and
turn beside doorframes, carry and throw objects, operate doors/levers/wheels,
watch enemies and ragdolls, climb ladders/ledges, inspect lantern shadows and
particle/rope motion, then pause, load a save and teleport. Include a GPU-limited
run below 60 FPS. Automated checks establish timing and state isolation; these
checks are still needed to assess complete maps, interactions and visual quality.
