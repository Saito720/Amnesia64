# Enemy_Llama local vision control

`Enemy_Llama` is an enemy entity controlled by an embedded local vision model.
Its body-mounted camera supplies the unlit scene, visible semantic masks, and
perceived sounds. The model chooses short actions for patrol, chase,
investigate, or attack; the engine executes movement, collision, animations,
and damage. Inference runs asynchronously through HPL2's llama.cpp service.
The observation preview and manual controls remain available without loading
a model or enabling the inference backend.

Only one Enemy_Llama is controlled in the active map. The eligible entity
with the lowest entity ID owns the observation, model requests, and manual
controls. Eligible means active, enabled, alive, and not pending destruction. Other Enemy_Llama
entities remain idle. Ownership automatically passes to the next eligible
entity when the current owner becomes unavailable; making a lower-ID entity
eligible again returns ownership to it. A pending response for a previous
owner cannot control the replacement.

## Creating an entity

In the rebuilt ModelEditor, open a copy of the desired enemy asset and choose
**Settings > User defined variables**. Select **Enemy_Llama**, choose **Grunt**,
**Brute**, **Suitor**, or **ManPig**, then save the copy as a new `.ent` file.
The editor registers this type automatically; no installed configuration
needs to be edited. The existing mesh, skeleton, and animation references
stay in the asset.

Changing type or subtype preserves matching customized settings. Click
**Apply rig preset** to replace the retained physical and animation-name
settings and the animation clip list with the selected rig's defaults. Clip
speed, special-event time, and sound/step events come from the original asset.
Camera settings, including `FOV`, hearing settings, and model-control settings
are preserved.
Type/subtype changes, individual variable edits, animation-dialog edits, and applying a preset are undoable
and mark the entity as modified. An open animation dialog follows undo/redo
of committed clips while retaining its draft across unrelated settings edits.

The variable tabs expose observation, model control, body, movement,
health/attacks, and animation names. Legacy sight ranges and state-machine
tuning are omitted.
The LevelEditor also recognizes the new type automatically, with its standard
enemy callback and sound-trigger instance settings.

Save the entity within its corresponding rig folder, alongside that rig's
animation assets. Presets retain the original relative resource paths, such
as `servant_grunt/animations/idle.dae_anim`; they do not search for or relocate
assets. Ordinary subtype selection keeps custom clip references and events.
**Apply rig preset** deliberately restores the selected rig's stock clips;
undo restores the previous list. The mesh still comes from the opened asset.

For manual asset creation, use a copy of an existing enemy `.ent` file and
retain its mesh, skeleton, animations, and physical variables. Set its
user-variable attributes to:

```xml
<UserDefinedVariables EntityType="Enemy_Llama" EntitySubType="Grunt">
    <!-- Retain the copied enemy's existing Var elements. -->
</UserDefinedVariables>
```

The available subtype presets are `Grunt`, `Brute`, `Suitor`, and `ManPig`.
The runtime selects the same `Enemy_Llama` loader for every subtype, then uses
that preset to supply missing physical and animation variables. Values
explicitly present in the `.ent` file take precedence. Subtype names are
case-insensitive; an empty or unknown subtype uses the Grunt preset.

Changing only `EntitySubType` on a complete copied entity keeps its existing
physical values. To adopt the selected preset, remove the corresponding
retained physical/animation `Var` entries from that copy, or use
**Apply rig preset** in the rebuilt ModelEditor. The runtime supplies only
absent variables and retains explicit customizations; the editor button
intentionally resets the selected rig's physical settings and clips.

A subtype does not enable the original enemy's AI or change its mesh. Keep
the desired mesh and animation references in the `.ent` file. This implementation
uses the biped pose; quadruped locomotion and rig-specific behaviors are not
implemented.

Preserve the common enemy settings for `Body_*` cylinder geometry and physics,
walk/run speeds and acceleration, turning limits, animation speed and movement
thresholds, health/toughness/regeneration, and attack dimensions/damage. Grunt
behavior rules, player sight/darkness ranges, and legacy state-machine timing
do not drive this entity. Its observation camera uses the entity's `FOV`
variable as its horizontal field of view in degrees, with a default of `120`.

Game assets and the installed editor's `EntityTypes.cfg` are not included in
this repository. Creating the rigged asset and testing it in a map therefore
require the user's game installation.

## Physical presets

The four presets retain the settings from the supplied enemy files: cylinder
physics, movement and turning, health/toughness/regeneration, hit effects,
attack dimensions/damage, and available movement animation names. They omit
legacy detection and behavior tuning. All four currently share health `100`,
toughness `0`, mass `50`, walking speed `1.3`, and running speed `5.1`.

| Setting | Grunt | Brute | Suitor | ManPig |
| --- | --- | --- | --- | --- |
| Body size | `1.2 1.85 1.2` | `1.5 1.85 1.5` | `1.5 1.85 1.5` | `1.2 1.85 1.2` |
| Mesh translation offset | `0 -0.02 0` | `0 0 0` | `0 0 0` | `0 -0.02 0` |
| Maximum turning speed | `12` | `10` | `10` | `12` |
| Walk-to-run animation threshold | `3.2` | `2.3` | `2.3` | `3.2` |
| Run-to-walk animation threshold | `2.8` | `2.1` | `2.1` | `2.8` |
| Normal attack damage | `30 60` | `50 120` | `50 120` | `30 60` |
| Normal damage type | `Claws` | `Slash` | `Slash` | `Claws` |
| Door attack damage | `26 26` | `50 56` | `50 56` | `26 26` |
| Door attack strength | `6` | `3` | `3` | `6` |

Brute and Suitor have matching numeric settings and their own hit/attack
sounds. The supplied `manpig.ent` has Grunt-equivalent numeric settings and
Grunt sounds. It references a static blockout mesh and lists only
`IdleExtra1`; the referenced mesh and breathing animation were absent from
the supplied Dark Descent game tree when these defaults were extracted.
The ManPig preset preserves that actual data. A playable rig still needs
valid mesh and movement-animation references supplied in the entity file.

The complete data tables are in `amnesia/src/game/LuxEnemy_LlamaProfiles.h`.
No mesh, texture, animation assets, or model weights are copied into this repository.
`amnesia/src/game/LuxEnemy_LlamaAnimations.h` stores the matching clip paths,
speeds, event timing, and events for the editor's preset action.

## Observation settings

Add these as `<Var Name="..." Value="..." />` entries inside
`UserDefinedVariables`. Defaults apply when an entry is omitted.

| Variable | Default | Meaning |
| --- | --- | --- |
| `FOV` | `120` | Horizontal observation field of view in degrees. |
| `LlamaObservationWidth` | `1280` | Observation image width in pixels. |
| `LlamaObservationHeight` | `864` | Observation image height in pixels. |
| `LlamaObservationInterval` | `0.25` | Seconds between observation captures. |
| `LlamaCameraOffset` | `0 -0.1 0` | Camera offset relative to the top of the character cylinder. |
| `LlamaHearingRange` | `12` | Maximum distance for perceived sound events, in game units. |
| `LlamaSoundThreshold` | `0.2` | Minimum perceived sound loudness. |
| `LlamaIdleAnimation` | preset | Optional idle animation name override. |
| `LlamaWalkAnimation` | preset | Optional walking animation name override. |
| `LlamaRunAnimation` | preset | Optional running animation name override. |
| `LlamaBackwardAnimation` | empty | Optional backward animation name override. |
| `LlamaDeathAnimation` | empty | Optional death animation name override. |

`FOV` is limited to 1-179 degrees at runtime; non-finite values fall back to
120 degrees. A copied asset's existing `FOV` remains an explicit customization,
and **Apply rig preset** preserves it along with the other observation settings.
Legacy AI and difficulty FOV multipliers do not alter the observation camera.
Image dimensions are limited to 64-2048 pixels per axis, the capture interval
to 0.05-5 seconds, hearing range to 0-100 game units, and sound threshold to
0-1. A zero hearing range disables sound evidence.

Grunt, Brute, and Suitor presets use `Idle`, `Walk`, and `Run`. The supplied
ManPig preset uses `IdleExtra1` and leaves its movement overrides empty.
None of the supplied four files defines `Backward` or `Dead`, so those
overrides are empty. Explicit names are useful for a custom rig; explicitly
empty overrides allow the loader to try available conventional names such
as `IdleBiped`, `WalkBiped`, and `RunBiped`. Missing movement
animations fall back to idle; if no conventional idle exists, the first
available animation is used. Missing death animations leave the mesh in its
current pose. A mesh with no animations remains usable as a static model.

The camera follows the character body rather than a skeleton bone, so skeletal
animation does not shake the observation. Its offset is independent of the
mesh's `Body_OffsetTrans` and `Body_OffsetRot` settings. The enemy's own mesh is
excluded from its observation while remaining visible to the player.

The observation contains the unlit scene and depth-tested semantic masks for
the player's cylinder (magenta) and currently breakable doors (cyan). Hidden
surfaces do not receive a visible mask. Depth is retained for future geometric
observations; this controller sends the color image and grounds attacks with
physics ray and collision checks. The final image is also available as packed,
top-down RGB8 pixels. Lantern masks and illumination evidence are not included.

## Model-control settings

These entity variables are independent of the rig preset. Explicit values
remain unchanged when applying another rig's physical settings and clips.

| Variable | Default | Meaning |
| --- | --- | --- |
| `LlamaControlEnabled` | `true` | Allow this enemy to use the shared local model controller. |
| `LlamaDecisionInterval` | `1.0` | Minimum seconds between submitted decisions, limited to 0.1-10. |
| `LlamaActionMaxSeconds` | `1.5` | Maximum lifetime of a movement action, limited to 0.1-2 seconds. |
| `LlamaAttackAnimation` | empty | Optional normal-attack animation name; empty uses an available conventional name. |
| `LlamaDoorAttackAnimation` | empty | Optional break-door animation name; empty uses an available conventional name. |
| `LlamaAttackCooldown` | `1.0` | Minimum seconds between attack starts, limited to 0.2-10. An active attack also blocks another attack. |

Non-finite decision interval, action duration, or cooldown values use the
defaults above.

The model returns one validated JSON decision with a behavior (`patrol`,
`chase`, `investigate`, or `attack`), action (`wait`, `move`, `turn`, or
`attack`), and optional target (`player`, `door`, or `obstacle`). Movement
specifies a forward/backward movement input, a relative turn of at most 90 degrees,
a duration, and whether to run. Targets are identified by normalized points
inside the current image. A short explicit memory string carries observed
evidence between requests, alongside the previous action's outcome.

The compact prompt treats the magenta mask as the approach target and assigns
the fictional enemy the objective of facing, approaching and performing
mechanical melee attacks on it. It asks for a small stationary turn toward an
off-center mask (negative left, positive right), using the effective authored
FOV and reducing the turn near the center or with a narrow FOV. It requests a
new observation before advancing. A target center within normalized x=450-550
permits forward movement through visibly clear space. Action duration remains bounded by
the entity's authored maximum. Complete JSON examples are omitted to reduce
copying an example instead of interpreting the image. This alignment rule is
an instruction to the model; the engine does not automatically steer it toward
the player. The four behaviors, sound/memory evidence and mechanical attack
validation remain available. Visibility alone does not prove melee reach.

Generation emits the normalized image coordinates `target_x,target_y` first,
followed by behavior, action, target category, forward input, turn, duration,
run and memory. This gives the model an opportunity to locate the visible target
before committing to its motor command. The parser retains the same ten fields
and accepts any order, including prior behavior-first replies. The ordering
does not supply engine-derived target coordinates or guarantee correct visual
grounding.

After an action ends, the next autonomous decision waits for a camera frame
captured at the resulting heading and position. Completion makes that capture
immediately due even when the authored observation interval is long; a frame
captured during the preceding action cannot supply the next decision.

Requests carry copied RGB pixels and the corresponding sound snapshot. They
never expose hidden player coordinates. Invalid, cancelled, failed, or stale
responses do not execute an action. Owner changes, map transitions, death,
disable/deactivation, and manual override invalidate pending work. Actions
expire instead of continuing movement indefinitely while a decision is slow.
The game thread applies decisions and checks the current owner and lifecycle;
the inference worker does not access gameplay objects.

Each owner has a persistent conversation. New observations, proposed actions,
and actual action outcomes extend that conversation; accepted history reuses
the model's KV cache instead of evaluating all prior turns again. Historical
images are timestamped evidence, not proof that a target is still visible.
Invalid, stale, cancelled, and truncated replies are excluded from accepted
history. A mechanically rejected attack remains a proposed command followed by
its explicit rejection outcome, rather than being remembered as a successful hit.

The controller waits for a move, turn, wait, or attack to finish before requesting
the next decision. Movement feedback reports actual horizontal distance and
achieved turning, including blockage or interruption. A bounded record of recent
engine-confirmed outcomes survives ordinary decisions and supplies the refresh
summary; it does not rely on the model's own memory string to detect repeated failures.

Attacks use the retained engine attack geometry, damage, strength, and
cooldown. The engine validates a visible target, reach, and occlusion before
striking a player, breakable door, or dynamic obstacle. A model cannot issue
scripts, teleport, or attack through a wall. Model behavior still needs
testing with the intended assets and maps; a valid decision does not guarantee
that the model chooses a useful route.

## Loading the local model

Build x64 with `HPL2WithLlama=true`; add `HPL2LlamaBackend=CUDA` to enable the
available NVIDIA backend. The [embedded inference documentation](../../HPL2/core/doc/LlamaInference.md)
describes prerequisites and build commands. CPU builds can run the same
controller, with latency determined by the local hardware and model.

Supply a language GGUF and its matching multimodal projector separately.
Configure the `Llama` section in the game's `config/game.cfg` using that
configuration file's XML attribute format:

```xml
<Llama
    Enabled="true"
    TargetSteeringOnly="false"
    ModelPath="models/Qwen3VL-2B-Instruct-Q4_K_M.gguf"
    ProjectorPath="models/mmproj-Qwen3VL-2B-Instruct-F16.gguf"
    ContextSize="16384"
    ContextKeepTurns="4"
    ContextRefreshFraction="0.8"
    MinImageTokens="1024"
    ImageTokens="2048"
    Threads="2"
    BatchSize="256"
    MaxTokens="192"
    MaxResultAge="6.0"
/>
```

Those model paths are the runtime defaults. Replace them with the files you
have, using paths relative to the game's working directory or absolute paths.
XML special characters in paths must be escaped. The game does not download
weights. Loading starts asynchronously when an eligible autonomous
Enemy_Llama is present or a perception diagnostic is requested. Missing files, unavailable inference support, or a
failed load leave it idle and expose the error in the debug preview.

Omit `GpuLayers` to select `99` when the compiled backend detects a supported
GPU, or `0` otherwise. Set `GpuLayers="0"` for explicit CPU execution. A
positive explicit value requests GPU offloading and requires a supported
device. A smaller positive value keeps some language layers in system memory;
the vision projector still uses the GPU. This permits larger models while
leaving GPU memory for the context cache, working buffers and game rendering.
The global `Enabled` setting
and the entity's `LlamaControlEnabled` setting must both permit control.

Set `TargetSteeringOnly="true"` for focused visual target alignment, following
and waiting. Each request uses one current image and the small three-field
steering protocol, without persistent conversation history or sound evidence.
The model chooses left/right turns, forward movement or wait; the engine maps
that choice to bounded movement without automatically aiming at the player.
The default `false` selects the full behavior policy, retaining patrol,
investigate, chase and attack. This option isolates basic visual steering;
the full policy's behavior still requires validation. F1's **Llama target
steering only** checkbox changes the selection for the current session and
the preview displays the active policy.

`MinImageTokens` and `ImageTokens` bound the projector's visual-token budget
(defaults 1024 and 2048); the minimum must not exceed the maximum. Qwen-VL's
projector warns that spatial grounding needs at least 1024 image tokens.
The default 1280x864 capture supplies 1080 tokens with Qwen3-VL's patch grid.
Patch rounding can change the count at other resolutions. Increasing the
maximum alone does not enlarge a small image; increasing a minimum can upscale
it but cannot recover missing detail. Update old entities' authored capture
dimensions as well as the model configuration. Each retained image consumes
context, and larger images increase inference work and GPU working memory.

The default context holds 16,384 tokens; the game allows 1,024-32,768. Images,
text, and model output all consume context. Increasing capacity also increases
KV memory requirements, and longer histories can increase decision latency.
`ContextRefreshFraction` defaults to `0.8` and is limited to 0.5-0.95. Near that
fraction, or when the next observation and reserved reply would not fit, the
worker rebuilds the context with the supplied factual summary and up to
`ContextKeepTurns` recent accepted turns (default 4, limited to 1-8), including
their image inputs. It keeps fewer turns when needed to fit. Refreshing context
does not unload the model. Owner/world/control changes start a fresh conversation;
transient context is not serialized into game saves.

Only one request runs for the owner at a time. Rendering and observation
captures continue while inference is pending. Before applying a response,
the controller rejects observations older than `MaxResultAge` (default six
seconds, configurable from 0.25 to 600), movement more than two game units from the capture position, or a
heading change greater than 45 degrees. It also checks map/owner identity
and lifecycle invalidation. Tune capture and decision intervals with the
intended hardware; the decision interval is a minimum cadence, not a promise
that inference finishes that quickly.
Large age limits support slow-model experiments and permit decisions from older
images. Attacks still require current physical reach, facing and unobstructed
geometry, and are revalidated at impact.

Changing configuration paths/settings requires restarting the game because
`game.cfg` is read at startup. If loading failed because weights were absent,
place the files at the configured paths, then toggle the debug VLM-control
option off and on to retry the load.

## Debug preview and manual control

Set `LoadDebugMenu=true` in the `Main` section of the game's configuration to
enable its existing debug menu. Open the in-game panel with F1 and check
**Enemy_Llama observation**. The owner is selected automatically according
to the lowest-eligible-ID rule, and the preview follows that owner.

While the F1 panel is open, Up/Down moves the owner forward/backward
at its walking settings, and Left/Right turns it using the shared turning
limits. With the observation option checked, opening F1 takes manual control
and invalidates pending model work. Closing F1 releases that override and
leaves the observation visible; autonomous control can then resume.
In perception-only mode, opening or closing F1 preserves the frozen diagnostic.
Unchecking the observation option clears its manual override and hides the
preview. Runtime capture and model decisions remain independent of the
preview checkbox. A short input timeout prevents continued manual movement
if debug updates stop.

**Enemy_Llama VLM control** toggles the shared controller for the current
session. Disable it for stationary observation/manual testing, then enable
it to resume autonomous decisions. This debug switch does not modify entity
files or the model configuration.

Check **Enemy_Llama perception only** to pause autonomous commands and inspect
one fresh frame with a stateless request. The query uses the exact RGB payload,
without action grammar, conversation history, sound events, or hidden world
information. It asks whether the magenta player and cyan doors are visible,
where the player appears, and what free space is visible. A description of
free space is not a physics or pathfinding guarantee.

The panel freezes the submitted frame and shows the model's reply, actual
visual-token count, latency, and the engine mask's pixel count and normalized
center for comparison. The engine mask measurements are debug information;
they are not supplied to the model. **Inspect current Llama frame** requests
another fresh image and also enables perception-only mode. The query does not
repeat until requested again. Its replies cannot execute actions.
Uncheck perception-only mode and close F1 to resume enabled autonomous control.

Each diagnostic saves the submitted image as PNG and a text report with the
prompts, reply, frame metadata and mask bounds under `llama_diagnostics` in
the game's save directory. This keeps full UTF-8 replies available outside the
bounded debug display. Map transitions, owner changes, death and deactivation
cancel pending diagnostics and release the frozen frame.

**Explain last Llama decision** pauses autonomous control and asks for a brief
prose explanation of the latest completed autonomous reply. It preserves that
decision's exact image, original system/user prompts, factual context summary,
raw reply and engine execution/rejection feedback, even after newer camera
captures or opening F1. Pending or cancelled requests do not replace the completed
record. A missing record produces a status message; changing owner/world or
making the owner unavailable clears the record.

The fresh explanation request does not use the action grammar or execute its
answer. It asks about player recognition, the interpreted goal, movement
commands, attack constraints and any reluctance about fictional game combat,
without assuming a refusal. Its panel/report identifies this as a retrospective
explanation: it cannot reveal or reliably reconstruct the original internal
decision process, and it does not include all earlier conversation images.
Explanations use the same PNG/text exports and token/latency reporting as
perception diagnostics. Uncheck perception-only mode and close F1 to resume.

The preview shows the captured RGB image, frame number, observation time,
resolution, effective horizontal FOV, camera position/yaw, and capture status.
It also shows behavior/action, request-in-flight state, last inference latency,
context tokens used versus actual capacity and percentage, reserved reply space,
text/image/output token counts, retained turns and images, reused prefix tokens,
budget-compaction count, last context-rebuild reason, and pending reply validation.
Total occupancy and image token counts are exact; rebuilding history can change
the split between text and output because of retokenization. Qwen's image
positional coordinates are not treated as token occupancy.
The panel includes model/controller status and bounded single-line excerpts of the model reply
and action outcome. Unsupported non-ASCII bytes appear as `?` in these debug
excerpts; the validated model memory retains its UTF-8 text. These human-readable details remain outside the image
pixels. Between captures, the panel shows the most
recent successful frame and its timestamp. A failed capture can clear the
image while showing the failure status. When no entity is eligible, the panel
reports that there is no active owner.

Perceived sound events come from the game's existing enemy-hearable sound
broadcasts. They are filtered by range and loudness, with bearing rounded to
15 degrees and distance to two game units. Bearing is relative to the enemy's
orientation when the sound was heard, with positive angles to its right;
turning afterward does not rotate that remembered bearing in the preview.
The model prompt rebases it using the heading stored at hearing and the
submitted camera's heading. Nearby
repeated events are coalesced by approximate world direction and distance;
up to eight events live for five seconds. Disabling or deactivating an enemy
clears its live sound evidence. The panel displays the latest four with age,
relative bearing, approximate distance, and loudness.
They do not identify a sound source as the player or provide hidden player
coordinates. Sound details are snapshotted on each successful capture, so
their ages and the image belong to the same observation frame.

Useful in-game checks include moving around corners and stairs, standing
inside and outside the camera's view, hiding behind opaque obstacles, and
placing breakable and nonbreakable doors in front of each other. Check that
the enemy remains visible from the player's camera while its own view omits
its mesh. Repeat with the intended rigs and verify idle/walk/run/backward and
death animation compatibility, save/load, map transitions, and inactive
entities. With several Enemy_Llama entities present, verify that only the
lowest eligible ID accepts controls and captures frames. Disable, deactivate,
or kill it to test handoff, then restore eligibility to test deterministic
priority.

## Compatibility with other editor builds

The rebuilt editors append the built-in definition to `EntityTypes.cfg` in
memory when no `Enemy_Llama` type exists. Existing types keep their order,
and a custom `Enemy_Llama` definition already in that file takes precedence.
The built-in rig defaults and the preset button use the same data table as
the game loader.

For another editor build, the adjacent
`Enemy_Llama.EntityTypes.fragment.xml` supplies the new type,
observation/control variables, and populated physical subtype presets in the format
parsed by the HPL editors. Its variables are flat for compatibility with older
parsers; the rebuilt editor's built-in definition uses tabs. It is a complete
additional type with its own retained physical fields, so it does not inherit
the legacy Enemy base class's detection settings. It is a merge fragment, not a replacement
`EntityTypes.cfg`.

1. Back up the editor configuration from your game installation.
2. Insert the fragment's `Type` element under the existing `Types` element in
   `EntityTypes.cfg`. Do not replace the surrounding types or base classes.
3. Restart those editors, load a copied enemy asset, select `Enemy_Llama` and
   the appropriate subtype, and verify its physical variable values before
   saving. When changing type, copy the original values if the editor applies
   schema defaults.

Subtype variables inherit the parent's observation/control settings and add their
own physical defaults. New variables start with those defaults, but switching
an existing asset's type/subtype preserves matching variable values in this
editor. Without the preset button, refresh them by removing the corresponding
values from the copied
`.ent` file before loading it, or replace them explicitly. Check custom
values before saving. Separate runtime enemy classes are unnecessary.

The native integration checks in `HPL2/tests/editor-llama` exercise the real
ModelEditor schema, popup, settings actions, and entity save/load using a
scratch asset layout. `HPL2/tests/enemy-llama` exercises the game controller's
ownership, sound memory, movement, death, and save restoration with synthetic
rigs. `HPL2/tests/observation` checks real OpenGL captures and mask occlusion.
Rigged assets still require in-game validation.

## Preset provenance

Source files were read from the user-supplied installation under
`D:\Steam\steamapps\common\Amnesia The Dark Descent`. The paths below are
relative to that game directory. SHA-256 identifies the exact source bytes
used to extract the settings; the original files remain outside this repo.

| Preset | Source file | SHA-256 |
| --- | --- | --- |
| Grunt | `entities/enemy/servant_grunt/servant_grunt.ent` | `636F04B77086CD10E46CF9DD1C238252E10F93C7B837E492E14C7D2CE7BE2EAB` |
| Brute | `entities/enemy/servant_brute/servant_brute.ent` | `F02AC72B012801462949043E571CEB20E9F3F9DDDF8267AED31AC372412133AE` |
| Suitor | `entities/ptest/enemy_suitor/enemy_suitor.ent` | `53B6645B0C456A7BB6FDA1F0848AE5D6893781EAE72A69CCB782AB81AB88538F` |
| ManPig | `entities/manpig.ent` | `F0680193D72EE1BED1DD29F3051E6A974C63B9635229244D6109CA7193057DF5` |

