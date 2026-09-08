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
  and Player-trigger policy. Choose Steam friends-only/public hosting or direct IP.
  Join through a Steam invitation, paste a numeric lobby code, or refresh the public
  session list. Friends-only lobbies do not appear in public discovery. The advanced
  direct-IP option accepts an IP address or hostname and optional port; IPv6 literals
  are supported. Escape closes the window.
- Defaults: **Steam, friends-only**, **4 players including the host**, client map
  changes **off**, every player may trigger Player callbacks **on**. The supported
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
The client cannot open the game debug menu; F3/fast-forward is disabled for everyone
in a session. Host debug map loads/reloads use a queued session-preserving path.
Offline save/load and autosave rotation are suppressed for multiplayer worlds; session
persistence and resuming a co-op campaign are not implemented.

## Authority and synchronization

The host assigns connection IDs; clients cannot choose another player's identity.
Peer 0 denotes the host. Map epochs invalidate delayed packets from previous maps.
The network update runs globally, while gameplay modules receive their normal updates
when local menus have switched away from the gameplay container. Joining/changing-map
clients enter a waiting container until their downloaded map has loaded.

Remote players render as solid, depth-tested cylinders using the standing/crouching
character dimensions. Positions interpolate for rendering. These are visual proxies,
not additional Newton characters with collision against other players.

Dynamic body IDs derive from stable body names; duplicate names are excluded rather
than assigned ambiguous IDs. Clients receive a reliable initial body burst. Awake
bodies are sampled at 20 Hz, sleeping transitions are sent reliably, and periodic
ground-truth snapshots correct drift. Newton keeps simulating contacts locally;
normal corrections bias linear/angular velocity. Initial sync, large errors, sleep,
and periodic ground truth can set the full transform. Send queues and retained initial
snapshots are bounded. Slow clients are disconnected if reliable shared state cannot
be delivered, rather than silently losing it.

Before starting a grab/push/slide/lever/wheel controller, a player must obtain an
exclusive host lease for the connected body assembly. Only its owner may submit that
assembly's state. The host checks reach, bounded/finite transforms and velocities,
sequence numbers, and lease tokens. Leases release on exit, cancellation, timeout,
disconnect, or map teardown. Physical throws preserve the owner's final velocity.
Common breakable-object events run through the host and invoke the usual client
break path, including its broken representation and local effects.

## Maps and scripts

The host transfers the entire XML `.map`, in reliable 32 KiB chunks, with a 16 MiB
limit and CRC32 check. Clients write it to a generated session cache below their save
directory and load that exact file. Retail maps/scripts are not overwritten. Old
downloaded map files are removed when replaced or disconnected; generated empty cache
directories may remain. Compressed-only `.cmap` hosting is not supported.

Validation checks XML nesting/size, required map structure, file indices, duplicate
object IDs, direct resources, and local `.ent`, `.mat`, `.ps`, and `.snt` dependency
graphs. It follows engine conventions for author-machine paths, compiled meshes,
material suffixes, localized/numbered audio, textures and cube maps. Missing assets
produce a descriptive disconnection message. Scripted resource creation/effects also
validate their file arguments. Opaque mesh internals and every deferred engine asset
lookup are not yet covered by a universal missing-resource collector.

Only the host loads and runs map/global scripts. A fixed registry broadcasts **165
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

- Complete replication of native non-script gameplay: pickups, inventory use and
  combinations, enemy AI/health, death/respawn, puzzle state, journals and quest progress.
  Players can currently diverge in these systems. Shared script item grants are supported,
  and each client's inventory survives ordinary map transitions.
- Serialize authoritative saved-map/entity state for revisits and late joins. Replaying
  current-map effects plus physics is insufficient to reconstruct every native property
  restored from the host's saved-map collection.
- General entity spawn/delete/replace lifecycle, including every break variant and
  procedural object. Body replication requires the corresponding body to exist locally.
- Precise per-player trigger enter/leave semantics, remote look-at/use-item callbacks,
  deferred trigger attribution, and full coverage of all script APIs.
- Internet playtests with sustained latency, jitter and disconnects, co-op save/resume,
  content-version manifests, and runtime missing-resource reporting for all asset managers.

## Verification

The implementation has been exercised with:

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
- All 33 retail campaign maps passing content preflight, plus malformed maps and
  missing-asset regressions.
- Real SDL/OpenGL/ImGui rendering and input capture, both advanced and campaign controls.
- Steam UI discovery/invitation/retry states and input suppression under the Steam
  overlay, including suppression of the underlying ImGui controls. Steam callbacks
  were stubbed for these UI assertions; no invitations were sent by the tests.
- Two actual game instances: campaign hosting/joining, matching 77 dynamic body IDs,
  reciprocal player poses, continued updates with pause/ImGui open, a rainy-hall to
  old-archives transition, retained inventory, and rejection of delayed old-map poses.
  This regression passed with both standalone and Steam SDK direct-IP transports.
- One actual game instance hosting a live friends-only Steam campaign lobby, advancing
  through menus, changing to Old Archives while retaining its lobby, and leaving cleanly.

Build the solution's Debug x64 game before engine-backed tests. Run the independent
protocol and Newton suites from the repository root:

```powershell
./tests/RunMultiplayerProtocolTests.ps1
./tests/RunMultiplayerWorldTests.ps1
./amnesia/src/game/tests/build_multiplayer_content_tests.ps1 -Run -RetailDirectory 'D:/path/to/Amnesia'
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
does not establish a relayed gameplay connection to a second player; a two-account,
two-machine Steam playtest remains required to verify that path end to end.
Full-game smoke tests use isolated test configurations/profiles and a bounded run;
they must not share a live player's configuration or overwrite retail assets.
See `tests/multiplayer/README.md` for profile cleanup, output paths and runner options.

## Source layout

- `HPL2/core/include/network` and `sources/network`: engine-level transport.
- `HPL2/dependencies/networking`: pinned dependency build, provenance and licenses.
- `LuxMultiplayer`: session lifecycle, transfer, map/trigger policy and global updates.
- `LuxMultiplayerWorld`: physics, poses, rendering and interaction leases.
- `LuxMultiplayerProtocol` / `LuxMultiplayerWorldProtocol`: explicit wire codecs.
- `LuxMultiplayerContent`: map/dependency validation.
- `LuxMultiplayerScript`: typed script-effect dispatch and nested-call protection.
- `LuxMultiplayerUI`: ImGui host/join controls and global SDL input/render integration.
