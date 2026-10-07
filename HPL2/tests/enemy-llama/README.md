Build the x64 Release game first. This test links the same game objects and engine
libraries, and needs an installed game's read-only `core`, GUI resources and
configuration files. It creates its own synthetic map, character cylinders,
meshes and animation clip; no authored enemy or map is changed.

```powershell
cmake -S HPL2/tests/enemy-llama -B bld/enemy-llama-runtime-tests -G "Visual Studio 18 2026" -A x64 -DAMNESIA_DATA_DIRECTORY="D:/Steam/steamapps/common/Amnesia The Dark Descent"
cmake --build bld/enemy-llama-runtime-tests --config Release
ctest --test-dir bld/enemy-llama-runtime-tests -C Release --output-on-failure
```

The test covers deterministic controller ownership, inactive/disabled/dead owner
handoff, rejected commands for extra enemies, raw entity FOV loading (default,
custom, clamped and nonfinite values), actual horizontal camera projections at
square, wide and portrait image sizes, actual RGB capture, pixel-for-pixel
agreement with the game's actual debug-preview gfx after GUI rendering, sound event
quantization and expiry, capture-consistent sound snapshots, coalescing across
turns and queue ordering, immediate-turn sound/camera alignment, custom camera
offsets, manual input timeout, gravity, single-clip idle playback,
death animation reuse, and restoration through the native save-data objects.
Save coverage exercises the in-memory snapshot/restore methods, rather than a
complete game save file. Rigged assets, authored maps and visual animation quality
still need in-game validation.

Controller checks inject completed replies through a friend test adapter while
keeping the inference service unloaded. They cover strict reply parsing, bounded
move/turn/wait actions, immutable submitted images, stale images/results, heading
and position changes, and cancellation on manual override or owner handoff.
Compact target-steering checks run a translated turn through the actual mover,
verify an absent-target wait leaves the body idle, reject contradictory visibility
and action reports without inventing motion, and ensure switching protocols stops
an existing action and cancels a delayed old-format reply.
Action feedback checks retain a move until completion before another observation
can be submitted, report measured horizontal distance and achieved/incomplete
turns, reject a newer camera frame captured before a turn finished, and schedule
an immediate post-turn capture even with a five-second authored observation
interval. The resulting camera heading becomes the next planning snapshot.
They detect full and partial real physics blockage, preserve the authored
speed-limit travel budget when movement input scales acceleration, record interrupted progress, preserve completed
feedback when a later reply is rejected, and keep the eight newest engine-owned
outcomes separate from model-written memory. Session summaries use only those
outcomes and the actual submitted mask/sound evidence; manual override, owner
unavailability and native save setup discard old factual session history.
Perception-mode checks stop a running movement action, keep the submitted RGB
immutable across later captures and F1 manual controls, keep diagnostic text out
of action execution and action replies, reject cancelled generations, and clear
the frozen frame on owner handoff or leaving diagnostic mode. A real 65×64
observation is rendered through the frozen preview's actual GUI to check upright
pixels and odd-width texture row alignment. The production PNG
export is decoded and compared pixel for pixel with the frozen top-down RGB.
Decision-explanation checks preserve the exact completed autonomous request and
reply separately from transient action state, then ask about that historical
frame after newer camera captures and F1 or control-mode changes. The diagnostic
uses a fresh conversation without an action grammar; its text never executes an
action or replaces the autonomous reply. Missing decisions and decisions from an
unavailable owner cannot be explained, and ownership, world and death transitions
discard the saved evidence. Model-written explanations are a debugging aid, not
proof of the internal cause of a decision.
Missing observation
dimensions select 1280×864; explicit small images and the 2048-pixel ceiling remain
supported.
Actual physics and rendered masks cover melee reach, physical occlusion, impact
revalidation, one hit per swing, cooldown across later decisions, animation event
timing, player/prop damage, breakable doors, and fresh save restoration during a
cancelled attack. A physics-only wall intentionally tests rejection even when a
visible mask remains in the submitted image. Mechanical attack grounding uses
the controller's configured result-age limit.

The normal test disables model loading. To also load a real local model and check
that its image-based JSON reply reaches the actual game controller, run the built
executable with an additional `--model <GGUF> --projector <mmproj-GGUF>` pair after
the asset-root and scratch-directory arguments. This optional check honors the
retail `Llama/MaxResultAge` setting, using the game's finite 0.25–600 second range
and six-second default. Its total deadline is 90 seconds of loading headroom plus
three times the rounded-up result-age budget (at most 1,890 seconds). GPU offload
and CPU thread settings also come from retail configuration. It logs the configured
age limit and each accepted decision's latency, and performs no downloads or retail
writes. It verifies three accepted
decisions with real physics ticks, completed/rejected action evidence, retained
image/output token counts in a 16K context, and reuse of the accepted conversation
cache. The live movement fixture uses 64×64 synthetic observations rather than the normal
1280×864 game capture, so its timings and decisions do not establish gameplay quality
or authored-map behavior. Those remain in-game checks.

For a focused visual grounding check, add `--perception-only` before the model pair:

```powershell
& bld/enemy-llama-runtime-tests/Release/hpl2_llama_runtime_tests.exe "D:/Steam/steamapps/common/Amnesia The Dark Descent" "D:/Amnesia64/bld/enemy-llama-perception/scratch" --perception-only --model "path/to/model.gguf" --projector "path/to/mmproj.gguf"
```

This mode skips the movement suite and uses a static 1280×864 camera with an
off-center magenta player cylinder and a cyan breakable door. A second observation
places an opaque rendered wall in front of the player. Each image starts a fresh
stateless conversation, uses the game's shared perception question without an
action grammar, and supplies no engine target location or pixel count to the model.
The game's configured model, projector, image token range, GPU offload and threads
are used. The test checks at least 1,024 actual image tokens, visible-player
recognition and localization within the submitted mask bounds, no occluded-player
hallucination, and visible-door recognition. It allows 300 seconds to load and
600 seconds per reply; these are diagnostic deadlines, not gameplay timing claims.
Exact top-down RGB PPMs, prompts, replies and numeric ground truth are written to
scratch. No actions execute and no diagnostic reply enters the enemy's action
history. Clear-route and scene descriptions are retained for human review rather
than treated as proof of navigable world geometry.

The local 30B-A3B Q4_K_M check on 2026-10-06 used 24 GPU layers, eight CPU
threads, and 1,024–2,048 image tokens. Both 1280×864 frames received 1,080 visual
tokens. The visible-player reply took 36.26 seconds and reported center `618,555`,
inside the engine mask bounds `600,492` to `641,629`. The independently occluded
frame took 18.13 seconds and correctly reported no player. Both recognized cyan
door presence. However, the visible scene description invented a second cyan
door and described a route between two doors when the fixture contains only one.
The focused assertions passed; these answers demonstrate target recognition and
occlusion handling in this fixture, with unreliable scene and route descriptions.

To exercise the actual autonomous command and explanation control on the visible
fixture, use `--explain-decision` in place of `--perception-only`:

```powershell
& bld/enemy-llama-runtime-tests/Release/hpl2_llama_runtime_tests.exe "D:/Steam/steamapps/common/Amnesia The Dark Descent" "D:/Amnesia64/bld/enemy-llama-explanation/scratch" --explain-decision --model "path/to/model.gguf" --projector "path/to/mmproj.gguf"
```

This submits one real autonomous request through the game's controller, preserves
its exact request and reply, advances the live camera, and invokes the public
decision-explanation control. The new prose query uses the original submitted
image and quoted input, output and mechanical feedback in a fresh conversation
without an action grammar. It checks the actual token budgets, frozen image and
input fidelity, and isolation from action execution and history. It also checks
that the clarified objective produces a chase or attack command toward the visible
player; that assertion runs after exporting the explanation, so a repeated patrol
still leaves useful evidence for review. The fixture allows 300 seconds to load
and 600 seconds per query, and writes exact images, complete prompts, replies,
token counts, elapsed times and the production PNG/text report to scratch. This
single decision does not establish sustained navigation or attack success.

The local 30B-A3B run on 2026-10-06 returned a patrol/right-turn command in
25.42 seconds despite the visible player and clarified pursuit objective. Its
fresh explanation took 77.31 seconds, used 1,080 image and 264 output tokens,
recognized the visible player right of center, and understood the pursuit goal.
It reported no combat reluctance and speculated about uncertainty or reorienting;
it also claimed the player was out of attack reach without sufficient supplied
evidence. The controller, exact-input preservation and explanation-isolation
checks passed, but the final chase/attack expectation failed. This reproduces the
behavior problem and verifies that the new diagnostic can inspect it; the reply
does not establish the original internal reason or rule out learned restrictions.

Generated resources, logs and test output stay in the build directory's `scratch`
folder. The executable rejects scratch directories within the retail asset tree.
Resource cache writing is disabled for the test.

For a focused production steering check, use `--steering-only` before the model
pair:

```powershell
& bld/enemy-llama-runtime-tests/Release/hpl2_llama_runtime_tests.exe "D:/Steam/steamapps/common/Amnesia The Dark Descent" "D:/Amnesia64/bld/enemy-llama-steering/scratch" --steering-only --model "path/to/model.gguf" --projector "path/to/mmproj.gguf"
```

This reuses the static full-detail room and actual depth-tested player mask,
placing the visible target right, left and center in three independent stateless
queries. This mode explicitly enables the controller's target-steering mode and
uses its small three-field JSON reply: target visibility, target side and action.
The user question contains no history or engine coordinates and has no system
prompt; the image supplies the target location. The normal native suite forces
this optional mode off, regardless of the retail setting. Controller and enemy
resets discard prior factual and model-written history. The check expects a
stationary chase turn with the correct sign for off-center targets, and forward
chase movement for a centered target. It verifies the actual translated engine
command: turns request five percent of the authored horizontal FOV, and movement
requests full forward input. Accepted commands run through the real enemy mover and
physics, with actual achieved yaw, displacement and completed action feedback
recorded. Exact images, complete prompts and grammar, raw replies, context token
counts, timings and JSON reports for all three scenarios are exported before
behavioral assertions, so a failed comparison retains the complete evidence.
Loading and query deadlines remain 300 and 600 seconds respectively. This verifies
single-step steering in a synthetic room; sustained navigation and authored-map
behavior still require in-game testing.
