# Enemy_Llama observation preview

`Enemy_Llama` is an enemy entity with its own body-mounted observation camera.
This first milestone previews the input intended for a later vision model.
It does not load a model, submit inference requests, or choose autonomous
behavior. The entity stays stationary until moved through the debug controls.
The preview requires neither `HPL2WithLlama` nor a GPU inference backend.

Only one Enemy_Llama is controlled in the active map. The eligible entity
with the lowest entity ID owns the observation and manual controls. Eligible
means active, enabled, alive, and not pending destruction. Other Enemy_Llama
entities remain idle. Ownership automatically passes to the next eligible
entity when the current owner becomes unavailable; making a lower-ID entity
eligible again returns ownership to it. The same ownership rule is intended
to guard future model decisions.

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
Camera settings, including `FOV`, and hearing settings are preserved.
Type/subtype changes, individual variable edits, animation-dialog edits, and applying a preset are undoable
and mark the entity as modified. An open animation dialog follows undo/redo
of committed clips while retaining its draft across unrelated settings edits.

The variable tabs expose observation, body, movement, health/attacks, and
animation names. Legacy sight ranges and state-machine tuning are omitted.
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
the desired mesh and animation references in the `.ent` file. This milestone
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
No mesh, texture, or animation assets are copied into this repository.
`amnesia/src/game/LuxEnemy_LlamaAnimations.h` stores the matching clip paths,
speeds, event timing, and events for the editor's preset action.

## Observation settings

Add these as `<Var Name="..." Value="..." />` entries inside
`UserDefinedVariables`. Defaults apply when an entry is omitted.

| Variable | Default | Meaning |
| --- | --- | --- |
| `FOV` | `120` | Horizontal observation field of view in degrees. |
| `LlamaObservationWidth` | `384` | Observation image width in pixels. |
| `LlamaObservationHeight` | `256` | Observation image height in pixels. |
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
Image dimensions are limited to 64-1024 pixels per axis, the capture interval
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
surfaces do not receive a visible mask. Depth is retained for future navigation
grounding. The final image is also available as packed, top-down RGB8 pixels.
This milestone does not add a lantern mask or illumination evidence.

## Debug preview and manual control

Set `LoadDebugMenu=true` in the `Main` section of the game's configuration to
enable its existing debug menu. Open the in-game panel with F1 and check
**Enemy_Llama observation**. The owner is selected automatically according
to the lowest-eligible-ID rule, and the preview follows that owner.

While the F1 panel is open, Up/Down moves the owner forward/backward
at its walking settings, and Left/Right turns it using the shared turning
limits. Closing F1 clears manual input but leaves the observation visible.
Unchecking the observation option or an ownership change clears the previous
observer's input. A short input timeout prevents continued movement if debug
updates stop.

The preview shows the captured RGB image, frame number, observation time,
resolution, effective horizontal FOV, camera position/yaw, and status.
Status distinguishes disabled, inactive/dead, waiting, and capture failures. These human-readable details
remain outside the image pixels. Between captures, the panel shows the most
recent successful frame and its timestamp. A failed capture can clear the
image while showing the failure status. When no entity is eligible, the panel
reports that there is no active owner.

Perceived sound events come from the game's existing enemy-hearable sound
broadcasts. They are filtered by range and loudness, with bearing rounded to
15 degrees and distance to two game units. Bearing is relative to the enemy's
orientation when the sound was heard, with positive angles to its right;
turning afterward does not rotate that remembered bearing. The heading at
hearing is retained for future navigation to rebase the direction. Nearby
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
observation variables, and populated physical subtype presets in the format
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

Subtype variables inherit the parent's observation settings and add their
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

