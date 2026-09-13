# Multiplayer implementation status

This is an experimental multiplayer foundation, not a campaign-complete co-op release.
It provides real host/client sessions, shared player and physics state, map delivery,
exclusive object interaction, host script effects, and coordinated map changes.
Windows Debug and Release x64 are the primary build targets for this checkout.

## Starting a session

- **Main Menu → Start New Game → Multiplayer → Host / Join** opens the streamlined
  Steam campaign controls. Hosting creates a friends-only Steam lobby and starts the
  configured campaign entry map in normal mode, independently of developer startup-map
  overrides. Use **Invite friends** or **Copy code** after the lobby has opened.
- **Grave / tilde (`~`)** opens the ImGui window from the main menu or gameplay.
  Its Host tab accepts a map, start position, player limit, client map-change permission,
  Player-trigger policy, and player collision. Choose Steam friends-only/public hosting or direct IP.
  Join through a Steam invitation, paste a numeric lobby code, or refresh the public
  session list. Friends-only lobbies do not appear in public discovery. The advanced
  direct-IP option accepts an IP address or hostname and optional port; IPv6 literals
  are supported. Escape closes the window.
- The advanced Host tab can host the **currently loaded map** without restarting
  the host's world, or **Browse...** for an XML map using folder navigation and
  selection. Current-map hosting preserves position, inventory and story mode,
  and shares removal of authored items already collected offline. Earlier scripted
  scenes and spawned objects cannot yet be reconstructed for joining players.
  The world must have been fully loaded from XML, and its parsed source must still
  match the file. Cached/partial worlds and edited sources require a fresh map load
  through Browse; offline cache loading remains unchanged. Clients inherit the
  host's current difficulty before loading.
  This option appears only for a loaded playable world, including a paused game;
  the title menu's background world cannot be hosted. The **Start position** list
  reads PlayerStart areas from the selected or entered XML map, with **Map default**
  available. Changing the map clears stale start selections. File reads are deferred
  until editing settles, and the list can be refreshed after editing a map externally.
- Defaults: **Steam, friends-only**, **4 players including the host**, client map
  changes **off**, every player may trigger Player callbacks **on**, player collision **off**. The supported
  player limit is 2–16. Direct-IP hosting uses UDP **27015** by default.
- Both computers require the retail game assets and matching multiplayer builds.
  Steam play additionally requires separate signed-in Steam accounts with access to
  AppID **1362050**. Steam handles discovery/signaling and relays game traffic; players
  do not need to configure a listening UDP port for Steam sessions. The host's machine
  still runs the authoritative world. Valve is providing relays, not hosting the game
  simulation. Direct-IP Internet hosting still requires a reachable address/port.
- End the session or disconnect in the tilde window. A client returns to the main menu
  on disconnection. Stopping a hosted session leaves the host's current world available.

Steam invitation callbacks are processed even at the main menu. An accepted Steam
invitation joins immediately while idle; during an existing session it appears in
the multiplayer window with an explicit replacement action. Steam's
`+connect_lobby <lobby ID>` launch argument is supported alongside the existing
configuration-file argument. **Retry Steam** retries initialization if Steam was not
running when the game opened. Failed initialization preserves the invitation for
another attempt. Offline play remains available when initialization fails. The Steam
overlay captures input without stopping the multiplayer world.

## Steamworks build and hosting

The default build uses the supplied Steamworks SDK **1.65**, including its
`ISteamNetworkingSockets` implementation. It replaces the standalone GNS runtime for
that build; physics replication and application packets are unchanged. The advanced
direct-IP option uses the same selected backend. Steam builds require an initialized
Steam runtime for either network mode. `/p:HplUseSteamworks=false` builds the original
standalone direct-IP backend without requiring a Steam account.

Steam initialization occurs before the graphics window so the overlay can attach.
Build output includes Valve's runtime DLL and a development `steam_appid.txt`, and
the initialized AppID is checked against this build's configured title. Keep the
executable and runtime together; use the retail installation as the asset working
directory. The build does not overwrite the retail game's AppID or DLLs. Do not ship
the development `steam_appid.txt` in a Steam depot; see the
[SDK/build instructions](HPL2/dependencies/steamworks/README.md) for packaging controls.

Steam connections address authenticated Steam identities. Hosts accept only members
of their compatible lobby and enforce connection capacity. Session ownership remains
with the original host: leaving or losing that host ends the session instead of
migrating the physics authority to another player. Public discovery filters for this
multiplayer protocol. Steam's P2P path has direct ICE routes disabled so it uses relay
connectivity. No Web API publisher key or custom authentication server is required.
Canceled asynchronous lobby requests are detached and any late successful lobby is
left. A replacement request waits for that result to avoid joining and then accidentally
leaving the same lobby. If Steam never returns the outstanding result, restart the game.

For development, grant testers access to your title through your Steamworks packages
or development keys. Owning retail Amnesia alone does not grant access to your separate
AppID. A full relay test requires two different Steam accounts; two processes signed
in as the same account do not represent two remote Steam players. This integration
does not configure or publish Steamworks partner settings or upload a depot.

Valve references: [Steam Datagram Relay](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay),
[Steamworks API setup](https://partner.steamgames.com/doc/sdk/api),
[lobbies and invitations](https://partner.steamgames.com/doc/api/ISteamMatchmaking).

The pause menu, inventory, journal, tilde window, and loss of application focus do not
suspend session/world updates. Voice completion callbacks also continue through menus.
Dragging a window border on Windows also keeps the game and session updating.
Resizing updates rendering and GUI layouts without changing the configured launch
resolution. Modal dialogs and the multiplayer map browser remain accessible after
shrinking the window.
The scene continues rendering behind the pause menu, inventory and journal. Death
closes these local menus and respawns that player at the shared checkpoint without
resetting the other players' world or enemies. Clients immediately show a connection
screen when joining. A host map change notifies connected clients before the host's
fade/save/load, so they show a preparation screen throughout that wait. Clients then
show local map verification, download progress (percentage and received/total bytes)
when needed, and the existing loading image while the map loads. Matching installed
or cached maps skip the download phase. Every waiting frame clears the viewport;
blocking verification and loading also present their screen before starting work.
Cancelled or failed host map changes return clients to the previous map, preserving
in-flight world events. Same-map start-position changes do not put clients into a
map-loading state. This transition handshake uses protocol **8**; all players need
the updated build.
The client cannot open the game debug menu; F3/fast-forward is disabled for everyone
in a session. Host debug map loads/reloads use a queued session-preserving path.
Debug loads now perform the original game's full new-game reset on every peer.
Ordinary level changes instead retain the original game's persistent state,
including flashbacks, and apply the host's final old-map script cleanup before
loading the next map. Native fade-in and player reactivation also run on clients.
Offline save/load and autosave rotation are suppressed for multiplayer worlds; session
persistence and resuming a co-op campaign are not implemented.

## Authority and synchronization

The host assigns connection IDs; clients cannot choose another player's identity.
Peer 0 denotes the host. Map epochs invalidate delayed packets from previous maps.
The network update runs globally, while gameplay modules receive their normal
PreUpdate, Update and PostUpdate phases when local menus have switched away from the
gameplay container. Hands and attached lights continue following the live camera;
menus retain input control, and the global scene/physics update is not repeated.
Joining/changing-map
clients enter a waiting container until their verified map has loaded.

Remote players render as solid, depth-tested cylinders using the standing/crouching
character dimensions. Positions interpolate for rendering. The host can enable
character collision in the advanced window; the matching Newton character proxies
do not push shared dynamic objects. Their collision surfaces also participate in
NPC character sweeps. Stale, disconnected and previous-map proxies are removed.
An equipped, visibly drawn lantern also contributes one remote point light, sampled
from the hand asset's `PointLight_1`. Its position, current color and radius follow
the native light, including flicker and the holster fade. The secondary hand light,
player helper fill light and lantern mesh are not replicated. Remote lights are
removed when the source disappears, its pose becomes stale, the peer disconnects,
or the map is replaced.

Dynamic body IDs combine the entity-qualified body name with its authored XML body
ID. Repeated names inside retail entities, including `chest_of_drawers_nice.ent`,
remain distinct. Procedural bodies without authored IDs use their stable names;
truly ambiguous identities are excluded. Clients receive a reliable initial body burst. Awake
bodies are sampled at 20 Hz, sleeping transitions are sent reliably, and periodic
ground-truth snapshots correct drift. Newton keeps simulating contacts locally;
normal corrections bias linear/angular velocity. Initial sync, large errors, sleep,
and ownership handoffs can set the full transform. Routine reliable corrections
converge without a forced position jump; displaced sleeping followers wake and
settle toward their resting pose. Rotation prediction follows angular velocity.
Send queues and retained initial
snapshots are bounded. Slow clients are disconnected if reliable shared state cannot
be delivered, rather than silently losing it.

Before starting a grab/push/slide/lever/wheel controller, a player must obtain an
exclusive host lease for the connected body assembly. Only its owner may submit that
assembly's state. The host checks reach, bounded/finite transforms and velocities,
sequence numbers, and lease tokens. Leases release on exit, cancellation, timeout,
disconnect, or map teardown. Physical throws preserve the owner's final velocity.
Leases also bind to each body's runtime instance. Scripted replacements cannot
inherit an old token or receive the former body's final transforms and flags,
even when their authored identity or allocated address is reused.
Common breakable-object events run through the host and invoke the usual client
break path, including its broken representation and local effects.
Objects owned by someone else no longer offer an interaction crosshair or outline.
Granting a door lease opens its host-side hinge limits and restores native auto-close
behavior. Only the host decides when to close it automatically.
Releasing a held prop preserves the native collision
safeguard until it clears the local player's body.

Player contact also obtains an exclusive simulation lease, held for one second
after the last contact. Each player may influence up to four separate assemblies;
contact requests are throttled and validated against recent player/body bounds.
Until granted, character collision still blocks the player while forces on the
object wait. This prevents a client from tipping an unowned chair locally and
walking through it. Deliberate interactions take priority over passive contact,
and contact never starts an interaction controller or disables door auto-close.
Accepted owner snapshots relay directly to observers rather than taking another
smoothing pass through the host's follower. Final state transfers reliably on release.

Item pickups and lamp ignition use exclusive host-approved transactions. The
collector alone receives the item or spends a tinderbox; successful pickup removes
the world item for everyone, including late joiners. Failed pickups leave it in the
world. Claims and removal history distinguish replacement objects from earlier
instances with the same authored ID, so stale approvals cannot collect a replacement.
Native pickup/ignition/interaction callbacks run once on the host according
to the Player-trigger policy. A client's diary pickup waits for its host callback's
decision before opening the journal, including `ReturnOpenJournal(false)` for
scripted visions; a delayed response cannot reopen the journal during death or loading.
Native entity snapshots carry active/interaction/effect
flags, lamp lighting, door lock/closed states, and hinge/slider limits. Changed states
are sent reliably, with periodic corrections. Static MoveObject transforms also
synchronize; their motors and collision-triggered stops remain host controlled.
Destroyed joints leave stable authored slots in save data and native snapshots.
Only the simulation owner can initiate a force-driven joint break. Client owners
request host approval using the current body lease and token, retaining the joint
until the host confirms its removal. Nonowners cannot break constraints from their
local corrective forces; explicit host script breaks remain authoritative.
Host deletion tombstones remove the matching client constraints, including during
late-join initialization. Engine destruction observers clear prop aliases and end
an active joint interaction before another update can use it. Entry validation also
rejects a pending interaction if its required joint disappeared before the lease
arrived. Detached break pieces unregister their former prop before deferred cleanup.
Native sounds and particles use shared world creation hooks rather than per-entity
effect messages. Physical player sounds also pass through the common playback
helper, retaining the game's own footsteps, landing, ladder, and water timing.
Character-origin scopes identify player-produced effects, including native liquid
splashes. The host validates client-origin effects and relays them without an echo
to their creator. Newton contacts, scraping, rolling, and joint effects remain local:
every peer already simulates them. Enemy, hazard, animation, light-flicker, and
personal camera/commentary presentation retain their local simulation. Source
attribution is independent of physics ownership. Existing replicated effects
remain on their established paths to avoid duplicate playback.
Shared sound/particle sources validate their resources before publishing an effect.
An unavailable native source does not make clients disconnect for presentation the
origin could not fully load; ordinary local fallback remains in control. Resources
published by a valid source remain required on the receiving peer.
Late joiners receive active loops and particles plus silent declarations for
retained sound handles. Stopped or already-playing reusable one-shots are not
replayed at join, but later stop/start cycles work normally. The source controls
replica lifetime through removal or map/session teardown.

## Maps and scripts

The host first sends the XML `.map` size, CRC32 and SHA-256 hash. Clients hash their
installed map and then check a persistent download cache. An exact match skips the
map download; otherwise the host sends reliable 32 KiB chunks, up to 16 MiB. Size,
CRC32 and SHA-256 are checked before loading. Cache files are rehashed on every use,
so changed host maps or damaged cache entries require a fresh download.

The Windows cache is `%LOCALAPPDATA%/HPL2/Amnesia/MultiplayerCache/`. Linux uses
`$XDG_CACHE_HOME` (or `~/.cache`), and macOS uses `~/Library/Caches`, with the same
`HPL2/Amnesia/MultiplayerCache` suffix. Persistent maps live in `objects/<sha256>.map`.
Installed maps load directly without a cache copy. Downloaded/cached maps load from
private working copies, removed on replacement or disconnect. The multiplayer
window's **Downloaded maps > Delete downloaded maps** button removes persistent
entries without touching active working copies, installed content or profiles.
The section also shows the total size and count of stored map objects, including
damaged entries that still occupy space. It refreshes on opening or deleting and
every five seconds while expanded; temporary active copies are excluded.
A crash can leave a working copy. Neither build lists this cache as a profile.

Multiplayer worlds use the verified XML rather than potentially stale compiled map
or AI caches, and do not write derived caches beside retail maps. Offline cache
behavior is unchanged. An old `multiplayer_cache` folder under the profile root can
be removed manually; the build does not migrate or clean up legacy profile folders.
Compressed-only `.cmap` hosting is not supported.

Validation checks XML nesting/size, required map structure, file indices, duplicate
object IDs, direct resources, and local `.ent`, `.mat`, `.ps`, and `.snt` dependency
graphs. It follows engine conventions for author-machine paths, compiled meshes,
material suffixes, localized/numbered audio, textures and cube maps. Missing assets
produce a descriptive disconnection message. Scripted resource creation/effects also
validate their file arguments. The host records which individual resources in a
typed script effect are unavailable locally. Clients allow native fallback for only
those arguments while still executing the command's other behavior, such as subtitles
or credits. Every resource that validated on the host remains required on clients,
including other resources in the same command. The packet's resource mask is bounded
and must refer to actual resource arguments. Sound preloading is an optional warm-up hint:
unusable `PreloadSound` requests are logged and skipped, matching the native
engine's tolerance for the retail scripts' raw sample names, obsolete references,
and typos. They do not disconnect clients. Actual sound playback uses the host's
per-resource rule; required map dependencies retain strict validation. Opaque mesh internals and every deferred
engine asset lookup are not yet covered by a universal missing-resource collector.
Extensionless GUI sounds prefer an existing sound entity and otherwise resolve raw
audio (including the retail Archives `react_scare6.ogg`). Directory-only optional
skybox/light texture references are treated as unset; actual missing files still fail.

Only the host loads and runs map/global scripts. A fixed registry broadcasts **167
typed native script effects**, including item grants, presentation, lighting, entity
properties and common creation/replacement operations. Clients do not compile script
text from the connection. Nested native helper calls are suppressed to avoid duplicate
effects. Current-map effect history initializes late joiners and is capped at 256 KiB;
after that limit, late joins are rejected until the next map. History replays transient
effects too, so a late joiner can see earlier overlays or hear earlier sounds.

The every-player option includes remote player bounds in host Player collision checks
and forwards validated interaction callbacks. The host-only option excludes them.
Remote collision proxies currently use bounding-box overlap, not exact character-shape
overlap, and treat the players' occupancy as a union. There is no priority ordering.
The map-change option gates client level-door requests and immediately attributed
remote script callbacks. Doors are resolved against the host's map, checked for lock
state and player reach, and use host-owned destination/sound values. Deferred timers
and other indirect script execution do not yet retain the originating player's identity.

## Remaining integration work

- Complete replication of native non-script gameplay: inventory use and
  combinations, enemy AI/health, all puzzle state, journals and quest progress.
  Players can currently diverge in these systems. Shared script item grants are supported,
  and each client's inventory survives ordinary map transitions.
- Campaign death scripts that require global checkpoint callbacks need an explicit
  shared policy; local multiplayer respawn deliberately does not replay those callbacks.
- Serialize authoritative saved-map/entity state for revisits and late joins. Replaying
  current-map effects plus physics is insufficient to reconstruct every native property
  restored from the host's saved-map collection.
- General entity spawn/delete/replace lifecycle, including every break variant and
  procedural object. Body replication requires the corresponding body to exist locally.
- Precise per-player trigger enter/leave semantics, remote look-at/use-item callbacks,
  deferred trigger attribution, and full coverage of all script APIs.
- Further Internet playtests with sustained latency, jitter and disconnects, co-op save/resume,
  content-version manifests, and runtime missing-resource reporting for all asset managers.

## Verification

Completed verification from earlier runs includes:

- User-reported multiplayer playtests using different Steam accounts/machines. The
  regressions below remain controlled local tests and do not replace another friend playtest.

- Actual GameNetworkingSockets loopback sessions in Debug and Release x64: reliable
  fragmentation, unreliable messages, session-full rejection, disconnect reasons, restart.
- Live Steamworks initialization under AppID 1362050 from the retail asset working
  directory; canceled/destroyed pending requests; friends-only lobby creation, identity
  and capacity metadata, map-name updates, host metadata tampering, and clean teardown.
  Valve's relay service reached **Current** readiness with 25 valid relays in this test.
- Final Steam-enabled Debug and Release x64 builds completed with zero warnings and
  errors. The Steam SDK direct-IP transport passed the same reliable/unreliable,
  fragmentation, capacity, disconnect and restart checks as the standalone backend.
- Steam lobby metadata validation and 30,000 malformed ID cases; bounded lobby-code
  parsing and launch arguments preserving existing configuration-file behavior.
- Protocol truncation/malformed-packet tests, finite-number and rigid-transform checks,
  path bounds, sequence wrap, CRC, and deterministic fuzz inputs.
- Actual HPL2/Newton tests: initial snapshot backpressure/retry, convergence with 20%
  snapshot loss, jointed-body contention, canceled/delayed grants, expiry, final throw
  velocity, and gravity restoration. One convergence run ended at about 3.3 cm error
  after 1.5 seconds; this is a controlled test, not an Internet latency guarantee.
- Newton regressions for closed-door leases, collision flags, standing/crouching
  player collision, proxy cleanup, static mover rotation/translation, slow motion,
  recovery after losing a final movement update, and collision restoration after
  releasing a prop overlapping the player.
- Newton contact regressions cover force gating, exclusive ownership, contact grace,
  explicit interaction priority, direct owner-state relaying, stale-packet rejection,
  and smooth convergence from a single sleeping-body position/rotation update.
- Two-instance effects regressions cover finalized sound/particle settings, local
  physics exclusions, loop/particle lifetime, native latch edges and silent state
  corrections, selected water-footstep samples without actor echoes, and live
  sound/particle baselines. These test the native water sound helper and generic
  body-source capture; they do not automate traversal through a liquid volume.
- All 33 retail campaign maps passing content preflight, plus malformed maps and
  missing-asset regressions.
- Real SDL/OpenGL/ImGui rendering and input capture, both advanced and campaign controls.
- Full-game recovery from death with pause, inventory and journal open on both peers;
  actual world render callbacks continue behind all three menus.
- Two-instance native regressions: exclusive host/client pickups, host callbacks,
  no-fuel ignition rejection, one tinderbox cost per ignition, already-lit lamp
  callbacks, shared door states, the Old Archives bookshelf's complete transform
  and stop, late-join entity baseline restoration, same-ID replacement pickups,
  stale claim rejection, diary callback presentation decisions, and rendering after
  disconnect. Host/client drawer and grab-release trials use unchanged retail assets.
- Steam UI discovery/invitation/retry states and input suppression under the Steam
  overlay, including suppression of the underlying ImGui controls. Steam callbacks
  were stubbed for these UI assertions; no invitations were sent by the tests.
- Two actual game instances: campaign hosting/joining, matching 77 dynamic body IDs,
  reciprocal player poses, continued updates with pause/ImGui open, a rainy-hall to
  old-archives transition, retained inventory, and rejection of delayed old-map poses.
  This regression passed with both standalone and Steam SDK direct-IP transports.
- One actual game instance hosting a live friends-only Steam campaign lobby, advancing
  through menus, changing to Old Archives while retaining its lobby, and leaving cleanly.

The latest Steam-enabled Debug and Release builds complete without compiler warnings
or errors. Protocol/Newton suites and content validation of all 33 retail maps pass.
The UI run `c0367b4967cd` passes playable-world hosting controls and deferred map-start
selection. Full two-instance run `0b402d98b1dd` passes per-resource script fallback,
native menu hand/light updates, remote lantern lifetime, retail painting joint
destruction, stable save/baseline state, and deletion during a pending drawer grant,
alongside the existing campaign, native interaction, effect, map transition and cache
regressions. It also verifies loaded XML provenance, joined Hard Mode, owner-approved
joint breaks with follower suppression, and silent sound baselines followed by restarts.
The Newton suite verifies same-ID replacement and reused-address lease invalidation.
Artifacts are under `bld/multiplayer-tests/`. These game tests use the
Steam SDK's direct-IP loopback transport; they do not replace cross-account relay
playtests. Test coverage is described in `tests/multiplayer/README.md`.

Build the solution's Debug x64 game before engine-backed tests. Run the independent
protocol and Newton suites from the repository root:

```powershell
./tests/RunMultiplayerProtocolTests.ps1
./tests/RunMultiplayerWorldTests.ps1
./amnesia/src/game/tests/build_multiplayer_content_tests.ps1 -Run -RetailDirectory 'D:/path/to/Amnesia'
./amnesia/src/game/tests/build_multiplayer_content_tests.ps1 -CacheOnly
./tests/multiplayer/run-ui.ps1 -RetailDirectory 'D:/path/to/Amnesia'
./tests/multiplayer/run-game.ps1 -RetailDirectory 'D:/path/to/Amnesia'
# Steam SDK validation and live lobby/relay initialization (one licensed account):
./HPL2/tests/network/RunSteamTransportTests.ps1 -Live -WorkingDirectory 'D:/path/to/Amnesia'
# Full-game Steam host/lobby/map-change smoke (one licensed account):
./tests/multiplayer/run-game.ps1 -Backend Steamworks -SteamHostOnly -RetailDirectory 'D:/path/to/Amnesia'
```

The networking dependency README contains the standalone transport test commands.
The default UI/full-game regressions build the standalone backend. Rebuild normally
after those tests to restore the Steam-enabled game. The one-account Steam smoke
does not establish a relayed gameplay connection to a second player. User playtests
have exercised that path; each new build still needs a two-account playtest for
latency, disconnects and campaign behavior beyond the controlled regression suite.
Full-game smoke tests use isolated test configurations/profiles and a bounded run;
they must not share a live player's configuration or overwrite retail assets.
The current session/lobby protocol is version **8**; all testers must use the new build.
See `tests/multiplayer/README.md` for profile cleanup, output paths and runner options.

## Source layout

- `HPL2/core/include/network` and `sources/network`: engine-level transport.
- `HPL2/dependencies/networking`: pinned dependency build, provenance and licenses.
- `LuxMultiplayer`: session lifecycle, transfer, map/trigger policy and global updates.
- `LuxMultiplayerWorld`: physics, poses, rendering and interaction leases.
- `LuxMultiplayerEntities`: approved pickups/ignition, removal history and native entity state.
- `LuxMultiplayerEffects`: shared native world/player presentation and effect lifecycle;
  locally simulated physics and already replicated/local presentation are excluded.
- `LuxMultiplayerProtocol` / `LuxMultiplayerWorldProtocol`: explicit wire codecs.
- `LuxMultiplayerCache` / `LuxMultiplayerMapHash`: verified persistent map storage and SHA-256 identities.
- `LuxMultiplayerContent`: map/dependency validation.
- `LuxMultiplayerScript`: typed script-effect dispatch and nested-call protection.
- `LuxMultiplayerUI`: ImGui host/join controls and global SDL input/render integration.
