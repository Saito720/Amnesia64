# Multiplayer integration smoke tests (Windows x64)

These tests require Visual Studio with the C++ workload, a Windows SDK, and
a retail Amnesia: The Dark Descent installation. The runners discover Steam
libraries or accept `-RetailDirectory 'D:/path/to/Amnesia The Dark Descent'`.

From the repository root:

```powershell
./tests/multiplayer/run-ui.ps1
./tests/multiplayer/run-game.ps1
# One-account full-game Steam lobby smoke:
./tests/multiplayer/run-game.ps1 -Backend Steamworks -SteamHostOnly
```

The default runs rebuild Debug x64 with the **Standalone** backend first, so
loopback regression tests do not require Steam or multiple accounts. Add
`-Backend Steamworks` to build/run with the SDK; real game tests then require a
running Steam client with access to the configured AppID. The UI harness stubs
Steam services and never sends invitations. Build and run with the same backend.
Switching a test to Standalone changes the normal Debug game build too; rebuild
the game without `/p:HplUseSteamworks=false` to return to Steam afterward.
The current session/lobby protocol is **13**; all instances must use matching builds.

For the duplicate-name reconstruction and reconnect regression, run:

```powershell
./tests/multiplayer/run-game.ps1 -Backend Steamworks -BackHallOnly
```

This loads the unchanged retail Back Hall on two real instances. Its authored
slime props include repeated names with distinct IDs. The test checks that each
survives initial reconstruction, receives its own native state, and remains
intact across repeated baselines and a client reconnect to the same host.
It also makes a normal trip to Study and back, verifying the saved per-ID states
before departure from Study and after reconstruction on both peers. Old Archives
is unsuitable for this persistence check because its script clears saved maps.
The transport suite separately covers Steam lobby lifecycle and direct-IP
reconnects; authenticated two-account retry instructions are in
[the transport checks](../../HPL2/tests/network/README.md).

To separate builds from execution:

```powershell
./tests/multiplayer/build.ps1 -Kind ui
./tests/multiplayer/run-ui.ps1 -SkipBuild -Width 1024 -Height 768
./tests/multiplayer/build.ps1 -Kind game
./tests/multiplayer/run-game.ps1 -SkipBuild -Port 27843
```

For enemy-damaged doors and native prop creation/removal, run:

```powershell
./tests/multiplayer/run-game.ps1 -Backend Steamworks -DoorBreakOnly
```

This uses Daniel's Room's retail Grunt and crowbar door. Real attack animation
events must replicate both damaged meshes, the broken leaf, surviving hinges,
and exactly one debris prop. Repeated baselines, reconnects and a Study roundtrip
must retain the broken door. The debris's authored four-second lifetime is
respected, including removal on connected clients and absence after rejoining.
The final phases check scripted damage sound/particle creation, rapid
break/reset/break, and props created by host-only break callbacks. A native
object destruction callback also creates and configures a prop before its
source's ID is retired; both peers must retain one instance with the same ID,
health, and interaction state after replication settles.

Steamworks Debug x64 run `fe8282df9884` passed the focused door checks. Full
two-instance multiplayer run `bc8637bf98b0` and all protocol suites also passed
with protocol 13. These live runs use local UDP peers, not separate-account
Steam relay connections.

For native window-close confirmation, run both destinations:

```powershell
./tests/multiplayer/run-game.ps1 -Backend Steamworks -QuitOnly
./tests/multiplayer/run-game.ps1 -Backend Steamworks -QuitDirectly -SkipBuild
```

Both modes send actual SDL quit and Escape events to host and client. They
exercise gameplay, pause, inventory, journal, options, the multiplayer overlay,
both pause-menu Exit choices, existing modal questions, repeated close requests,
the return-to-game fade, GUI recreation and container replacement. Selecting No
must preserve the session, map and inventory. An accepted Yes must survive a
container replacement without asking again. `-QuitOnly` checks that
`ExitMenuDirectly=false` returns to the main menu, then confirms a title-screen
close exits the application. `-QuitDirectly` checks that
`ExitMenuDirectly=true` exits the application from a live session. Success is
recorded only after the real engine loop returns.

Verified with the Steamworks Debug x64 build: return-to-menu run `e16056fdb5c2`,
direct-exit run `a843a6449e1b`, and UI run `4855c75b6a07` passed. The native
quit runs use two local UDP peers with the Steamworks networking backend.

For a focused native Graphics-menu test, use one isolated instance and exit
before loading a campaign or starting a session:

```powershell
./tests/multiplayer/run-game.ps1 -Backend Steamworks -SettingsOnly -SkipBuild
./tests/multiplayer/run-game.ps1 -Backend Steamworks -SettingsOnly -SkipBuild -BorderMode Borderless
```

This reuses the normal Create Profile flow, rendered options screenshot, and
FPS/V-sync tests. It verifies that routine config saving preserves the absent
legacy Auto key or an explicit mode, then checks Borderless label/layout/controller navigation,
fullscreen exclusivity, Cancel, restart notices, saved preferences, and that the
current window remains unchanged until restart. `-BorderMode Auto` (the focused default)
omits the new setting to exercise legacy resolution matching; `Bordered` and
`Borderless` write explicit false/true choices. Combine with `-Width`/`-Height`
matching the monitor to check legacy borderless startup and its explicit bordered
override. Regular multiplayer runs default to explicit `Bordered` so resize tests
can continue to exercise decorations at monitor-native resolutions. Test profiles
remain isolated and are cleaned by the same runner.

For a focused shared-effects regression, start directly in Old Archives and
skip the unrelated campaign, inventory, and drawer phases:

```powershell
$env:CODEX_MP_INCIDENTAL_ONLY='1'
try { ./tests/multiplayer/run-game.ps1 -Backend Steamworks -SkipBuild }
finally { Remove-Item Env:CODEX_MP_INCIDENTAL_ONLY -ErrorAction SilentlyContinue }
```

This still uses two real instances, the normal transport, replicated player
poses, native sound entities and particle systems, and the player sound helper.
The normal full run includes these same cases.

For the remote lantern, menu updates, and broken-joint lifecycle cases alone,
use `CODEX_MP_LIFECYCLE_ONLY` in place of `CODEX_MP_INCIDENTAL_ONLY` above. This
also starts in Old Archives and runs both peers through the native lantern
inventory/hand-object paths and the unchanged retail painting and drawer assets.

For window-resizing integration coverage, run the complete suite with:

```powershell
$env:CODEX_MP_RESIZE='1'
try { ./tests/multiplayer/run-game.ps1 -Backend Steamworks -SkipBuild -Width 338 -Height 1000 }
finally { Remove-Item Env:CODEX_MP_RESIZE -ErrorAction SilentlyContinue }
```

For uncapped rendering integration, set `CODEX_MP_UNCAPPED=1` in the same way;
combine it with `CODEX_MP_RESIZE=1` to exercise both features together. The fixture
first checks fresh Uncap FPS/V-sync defaults, then disables V-sync in its isolated
instances to obtain extra rendered frames. It checks that those extra frames
neither advance the controlled character nor send additional network poses.
Native Graphics-menu tests cover labels/layout, gamepad navigation, Cancel, OK,
save/reload and restoration. Interpolation fixtures cover hard network corrections,
ownership immunity, remote cylinder/lantern alignment, and native hand bones and
vertex buffers across moving and stationary render samples. Normal runs explicitly
retain capped rendering for comparison. Both modes continue through the existing
two-instance physics, menus, map/cache transitions and disconnect tests.

This adds native GUI popup shrink/restore and nested-focus checks, plus an actual
main-menu confirmation resized from 1920x1080 to 640x480. On Windows, each peer
holds a native sizing operation for two seconds while the test checks real game
updates, outgoing pose sequences, and incoming peer freshness. Pause, inventory,
and journal also resize during the lantern fixture. The normal engine loop handles
these window changes; the fixture checks resized outline/depth attachments and
framebuffer completeness after rendering. The full map-change and teardown checks
then continue at the resized resolution. The separate
[engine suite](../../HPL2/tests/WindowResize/README.md) covers all native sizing
edges, fullscreen/minimize/restore, GPU storage, and rendered pixels.
The optional `-Width` and `-Height` set the initial game window (defaults: 800x600).
Portrait startup and subsequent portrait, widescreen, and ultrawide resizes also
check the live HUD and shared GUI projections. A square and native text render
through HPL into an isolated target using the HUD's current size and offset;
portrait pixel bounds must stay square, while wider windows must match frozen
retail canvas and pixel baselines. Retail's widescreen mapping has unequal X/Y
scale, which is part of the existing artwork/font presentation. Centering and
authored bounds remain checked for both modes, including other authored aspects
and continuity around the transition. The saved aspect-probe screenshots make
changes visible independently of framebuffer-dimension checks. A real production
loading-screen capture at 1600x900 also supports direct comparison with retail.

The UI test links real HPL2, SDL2, OpenGL, and Dear ImGui. It includes the
production overlay source and extracts the unchanged production input update
method; only application/coordinator services are stubbed. It verifies tilde,
Escape, key releases, game/menu input suppression, transfer input suppression,
cursor restoration, the global render hook, campaign defaults, advanced
settings, deferred actions, and teardown. PNG screenshots are saved for review.
The downloaded-map controls also verify lazy/throttled cache-size queries,
refresh after reopening/deleting, and populated/empty size-and-count displays.
Advanced-host tests cover current-map selection for a playable world, hiding the
option for a title-menu background, stale-action rejection, and XML-only file
browsing with selection/cancellation and navigation through extensionless folders.
Start-position cases parse authored PlayerStart areas, preserve their ordering,
ignore duplicate/empty names, and retain inactive starts to match the debug menu.
They check deferred parsing after browser or typed-path changes, stale-selection
clearing, the map-default choice, no-start maps, and malformed/missing map feedback.
The overlay and an open map browser also shrink to 320x240 and expand again,
checking their bounds and saving screenshots at the minimum supported window size.

The full-game test links the current game objects except the normal entry
point. Two hidden game instances run on loopback. It loads the retail campaign,
starts hosting an already loaded offline world while preserving its player state,
and verifies an offline-collected authored item does not return for a joining client.
Native regression phases also exercise cross-player crafting, removal of an
equipped ingredient, manual crafting with consumed item references, cancellation
of destroyed chests' purchase questions, host-only diary completion, restored props and attachments,
static saved poses, same-resource replacements with fresh joints, and a client-only
same-map teleport. These use production inventory callbacks and network handlers.
The offline fixture loads an isolated XML/script copy, verifies its loaded-source
fingerprint rejects semantic edits but accepts formatting changes, and retains
Hard Mode until both peers confirm the joined difficulty.
It then verifies matching installed-map reuse, initial dynamic bodies and remote player poses, leaves
pause and ImGui menus open while the network world advances, verifies real world
rendering and local death recovery from pause, inventory, and journal, changes the host
map to the Old Archives, and verifies inventory persistence and rejection of
old-map UDP poses. It also saves an actual main-menu multiplayer screenshot.
Matching retail XML must be loaded directly with zero downloaded XML bytes;
map changes and disconnect must preserve those installed files. Downloaded maps
use private working directories under the operating-system cache
(`HPL2/Amnesia/MultiplayerCache`), outside the game profile. No
`multiplayer_cache` directory may be created in either fresh profile, and hosts
must never own a received-map path.
In Old Archives, it checks exclusive host/client item pickups and host-only
callbacks, failed ignition without fuel, successful ignition charging only its
initiator, and callback-only interactions with an already lit lamp. It compares
door flags and all 16 components of a static bookshelf's completed transform,
including the replicated stop event. Finally, it replays the production initial
entity baseline against a deliberately stale client to verify collected items
stay removed and current lamp/door state is restored without duplicate callbacks.
Replacement-item trials reuse both the name and authored ID, checking that stale
claims cannot consume a replacement and a legitimate new pickup still succeeds.
The host creates these fixtures and clients await their canonical replicas,
including replacements that arrive before the client advances its test phase.
The lamp baseline also repairs mismatched light effects when its lit flag already matches.
Retail diary trials check suppressed, default, and explicit journal-opening decisions,
host-only trigger policy, and unchanged host pickup behavior. Responses must match
the pending pickup's token and name, and cannot reopen a waiting client.
It then creates two unchanged retail `chest_of_drawers_nice.ent` instances on the
host, awaits their client replicas, verifies that their identically named bodies
have distinct network IDs, and opens a drawer through the real slide controller first as host and then as
client. It checks exclusive ownership across each cabinet's three drawers,
replicated movement, and lease release. Host/client grab trials preserve collision
suppression on an overlapping drop until the native player-clearance check restores it.
A native movement trial pushes a retail wooden box: collision must stay solid while
ownership is pending, client contact must acquire simulation authority, and a short
separation must retain ownership before release and host/client convergence.
The sound regression sends all ten reported retail `PreloadSound` hints, two
trailing-space variants, and a valid sound entity through the host's actual
script entry point. A following reliable effect checks that the client processed
them and stayed connected. Additional script-resource cases target unavailable
host sounds/particles and commands with multiple resource arguments: the host's
per-resource mask must preserve native optional fallback and non-resource behavior,
while a different resource that is valid on the host remains strict on clients.
Decoder cases also reject malformed masks and unflagged missing playback resources.
Shared native world effects check origin-side resource validation so an unavailable
sample is not published as a required client asset.
Client phase observation verifies the waiting container, input state, inactive
old world and gameplay viewport, nonempty status, and monotonic bounded download progress. Screenshots
capture connecting, preparation, and actual intermediate download progress.
The normal campaign transition is held by the test until the client renders
preparation, proving notification precedes the host's synchronous load. Additional
trials verify same-map teleports leave clients playing and failed normal/debug
map changes from inventory, journal, and pause restore the original map,
epoch, gameplay viewport, and input. Synchronous
checking/loading draws occur inside packet handling; normal frame callbacks
cannot sample those short phases reliably.
Incidental-effect checks use common native sound/particle creation to verify
host world presentation and both players' effects arrive once with finalized
transforms, volume, attenuation, and particle color. Client native shadows stay
alive and muted while the authoritative instance presents. Native local and
physics scopes remain audible and uncaptured. State changes, sound stop, particle
kill, and destruction propagate. The actual player sound helper preserves its
selected sample and attenuation; GUI and typed script effects avoid duplicate
capture. Runtime effects never enter script history.
Native checks also exercise the real door latch and silent state correction,
water-step sample/gain/attenuation without an actor echo, and initial-state replay
of an active looping sound and particle system after clearing only those fixture
replicas through the production decoder.
Retained stopped loops and reusable one-shots receive silent late-join declarations;
later restarts must play without an actor echo. An already-playing reusable one-shot
must remain silent through ordinary updates until its next stop/start cycle.
The lantern fixture targets the actual visible hand asset's `PointLight_1`, with
one remote point light and no remote lantern mesh or helper fill light. It checks
source color/radius/position, continued oil consumption and corrected hands/camera
attachment while pause, inventory and journal are open, and light removal after
holstering or oil depletion. Native PreUpdate/PostUpdate forwarding
must preserve menu input control and avoid a second scene/physics update.
Joint lifecycle cases use an unchanged retail painting constraint and authored
drawer joints. They target destruction notification, stable broken save slots,
host deletion tombstones, repeated baseline application, and a delayed lease grant
after its required joint disappears. Released bodies
must remain usable without accessing the deleted joint or reviving its constraint.
Client-owned painting trials suppress host/follower threshold breaks and verify
owner requests produce host-approved removal even below the host's force threshold.
Invalid ownership, body IDs, tokens and nonbreakable joints cannot authorize removal.
The independent Newton suite replaces a leased body under the same authored ID
before the next update, checking stale owner-state rejection, reliable revocation
and unchanged replacement physics. Runtime-identity changes also cover address reuse.
After the drawer trials, the host writes a uniquely named copy of Old Archives
inside the test output directory, adding a unique XML comment. Four synchronized
loads check the cold download, a subsequent zero-byte persistent cache hit,
same-size cache corruption followed by rejection/download/repair, and a host edit
under the same filename followed by a new hash and download. Each load verifies
the content hash and removal of the previous private working copy. Cold transfers
must render intermediate download progress; matching installed/cached maps must
not enter the download phase. Disconnect
removes the final working copy. Once both sessions stop, the fixture deletes
only the persistent entries identified by this run's unique hashes; it never
uses the global cache-clear operation or changes retail assets.
The first cache transition uses a native level change and checks persistent effects
and player settings, plus final old-map script cleanup received after preparation.
Subsequent debug transitions must perform the native full reset on both peers.
Offline autosaves must be rejected both during the session and after a local
disconnect while the multiplayer world is still loaded. The title menu must
continue rendering after disconnect/reset without retaining a deleted world.
The test checks networking and simulation state; it does not measure latency
or prove all campaign scripts and physics interactions work correctly.
The reviewed protocol-8 Steam-enabled Debug and Release builds pass without compiler
warnings or errors. Full two-instance run `0b402d98b1dd` and UI run `c0367b4967cd` pass,
along with the protocol/Newton suites and content checks for all 33 retail maps.
These are controlled local regressions; cross-account relay playtesting remains separate.

The window-resizing integration also passes Steam-enabled Debug and Release builds
without compiler warnings or errors, combined game run `10fb37920780` with
`CODEX_MP_RESIZE=1`, UI run `a1774bfadfb4`, and 890 native engine assertions including
the optional fullscreen and minimize/restore cases. Harness linking emits the
existing nonincremental-link warning; the UI harness also reports its existing
compiler option warning. Neither game build emits those harness-only warnings.

The portrait regression reproduced a 68x266 square in a 338x1000 window in
`fd3f9907bd28`; it must render 68x68 after the narrow-window fix. Earlier tests
also required a square at 1920x1080, overlooking retail's intended 266x288
presentation. That assertion incorrectly accepted wider artwork in runs
`6ed6f723991f` and `757d547ee494`. The revised checks explicitly preserve the
retail widescreen baseline alongside the portrait correction.
The revised tests reject the widened build in `0bb48c6b3a24`. Corrected run
`da35169d9d20` passes the full multiplayer resize regression, including 68x68
portrait and retail 222x240/266x288 widescreen probe dimensions. The production
1600x900 loading-screen capture matches the supplied retail reference across
all 1,290,000 RGB pixels in the compared interior artwork/text region, excluding
window decoration. Both Steam-enabled game builds pass without compiler warnings
or errors.

For focused native-interaction debugging, set `CODEX_MP_NATIVE_ONLY=1` in the
runner's environment. The same two-instance harness starts in Old Archives and
skips the campaign/menu/death stages. Remove that environment variable for the
complete regression run. Both modes include the map-cache negotiation trials.
Shelf progress diagnostics include real transforms;
the legacy `GetMoveState()` ratio is unreliable for this retail fixture's tiny
off-axis starting rotation, so pass/fail uses actual rotation and translation.

`-EnemiesOnly -Backend Steamworks` runs a focused two-instance enemy regression.
The runner copies Old Archives into the run directory, adds inactive authored
retail grunt, brute and water-lurker assets plus a navigation grid, and loads it
through normal hosting and map transfer. Identical static test geometry above
the retail level isolates line-of-sight and melee collisions. The first encounter
uses normal movement; subsequent sensing and attack stages freeze enemy movement
while retaining native AI, animation attack markers and all networking.
The fixture checks client-only detection/chase with a distant host, attack query
proxies when player collision is disabled, stable target choice, occluded-player
memory, damage to each intended victim without duplicate application, brute and
water-lurker detection, death/disconnect cleanup, a real reconnect into a paused
mid-attack baseline, old-epoch rejection and map/session teardown. Native death
coverage includes exactly zero health and the normal respawn fade. A lit client
encounter screenshot and mesh/animation assertions check the rendered enemy.
Codec checks
also cover prefix truncation, non-finite values, excessive/duplicate animation
and light layers, identity generations, removal tombstones and reordered packets.
The live decoder also receives an unreliable replacement before its reliable
baseline: confirmation must retain the newer pose and keep the old native instance
alive until the replacement script can use it. The unique generated map's cache
object is removed on completion or failure after verifying its path and hash.
The ordinary complete suite is unchanged unless this mode is selected.

Verified on 2026-09-13 with Debug x64 and the Steamworks backend: the two local
instances passed the complete enemy fixture (final run `e78850edb034`, including
inherited fear cleanup before a session target exists). The full existing multiplayer
regression also passed as `d51f04166657`. The standalone
protocol and real Newton tests also passed, including host/client damage and
respawn-life rejection, bounded hearing events, collision with remote players in
both player-collision modes, raw collision poses independent of render smoothing,
collision-mask restoration, and deferred enemy contact lease reclaim with an
assembly-wide grace period. This exercises local UDP sessions using the Steamworks
networking backend; separate-account Steam relay playtesting remains distinct.

`-SteamHostOnly -Backend Steamworks` runs one actual game instance against the
configured Steam AppID. It starts a friends-only campaign lobby, verifies world
updates through menus, changes to Old Archives while retaining the same lobby,
checks save suppression, and leaves the lobby. It uses the signed-in Steam account
and sends no invitations. This tests Steam hosting integration, not a two-account
P2P gameplay connection. Test the latter on separate machines/accounts with access
to the title, using the in-game lobby code or a Steam invitation.

The UI process is limited to 20 seconds. Full-game processes have a 180-second
internal limit and a 200-second external limit, including cleanup on failure.
The Steam-host-only mode retains its 90-second internal limit. The full run's
additional time accommodates four real map loads for cache negotiation plus
replacement-item, grab-release, and menu-transition regressions. Individual phases
retain their shorter failure timeouts.
The full-game runner uses fresh `Documents/Amnesia/codex-multiplayer-smoke-*`
directories and deletes only the exact directories it created after copying
their logs. Use `-KeepProfiles` to retain those profiles for debugging. Retail
assets and existing profiles are unchanged; asset cache saving is disabled.
Artifacts and generated build files stay under `bld/multiplayer-tests/`.
