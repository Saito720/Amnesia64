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

To separate builds from execution:

```powershell
./tests/multiplayer/build.ps1 -Kind ui
./tests/multiplayer/run-ui.ps1 -SkipBuild -Width 1024 -Height 768
./tests/multiplayer/build.ps1 -Kind game
./tests/multiplayer/run-game.ps1 -SkipBuild -Port 27843
```

The UI test links real HPL2, SDL2, OpenGL, and Dear ImGui. It includes the
production overlay source and extracts the unchanged production input update
method; only application/coordinator services are stubbed. It verifies tilde,
Escape, key releases, game/menu input suppression, transfer input suppression,
cursor restoration, the global render hook, campaign defaults, advanced
settings, deferred actions, and teardown. PNG screenshots are saved for review.
The downloaded-map controls also verify lazy/throttled cache-size queries,
refresh after reopening/deleting, and populated/empty size-and-count displays.

The full-game test links the current game objects except the normal entry
point. Two hidden game instances run on loopback. It loads the retail campaign,
verifies matching installed-map reuse, initial dynamic bodies and remote player poses, leaves
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
The lamp baseline also repairs mismatched light effects when its lit flag already matches.
Retail diary trials check suppressed, default, and explicit journal-opening decisions,
host-only trigger policy, and unchanged host pickup behavior. Responses must match
the pending pickup's token and name, and cannot reopen a waiting client.
It then loads two unchanged retail `chest_of_drawers_nice.ent` instances on each
peer, verifies that their identically named bodies have distinct network IDs,
and opens a drawer through the real slide controller first as host and then as
client. It checks exclusive ownership across each cabinet's three drawers,
replicated movement, and lease release. Host/client grab trials preserve collision
suppression on an overlapping drop until the native player-clearance check restores it.
The sound regression sends all ten reported retail `PreloadSound` hints, two
trailing-space variants, and a valid sound entity through the host's actual
script entry point. A following reliable effect confirms the client processed
them and stayed connected. Direct decoder checks keep malformed preload packets
and missing actual playback sounds rejected while optional missing hints succeed.
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
Offline autosaves must be rejected both during the session and after a local
disconnect while the multiplayer world is still loaded. The title menu must
continue rendering after disconnect/reset without retaining a deleted world.
The test checks networking and simulation state; it does not measure latency
or prove all campaign scripts and physics interactions work correctly.

For focused native-interaction debugging, set `CODEX_MP_NATIVE_ONLY=1` in the
runner's environment. The same two-instance harness starts in Old Archives and
skips the campaign/menu/death stages. Remove that environment variable for the
complete regression run. Both modes include the map-cache negotiation trials.
Shelf progress diagnostics include real transforms;
the legacy `GetMoveState()` ratio is unreliable for this retail fixture's tiny
off-axis starting rotation, so pass/fail uses actual rotation and translation.

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
