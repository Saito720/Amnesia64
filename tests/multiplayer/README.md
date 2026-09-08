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

The full-game test links the current game objects except the normal entry
point. Two hidden game instances run on loopback. It loads the retail campaign,
verifies map transfer, initial dynamic bodies and remote player poses, leaves
pause and ImGui menus open while the network world advances, changes the host
map to the Old Archives, and verifies inventory persistence and rejection of
old-map UDP poses. It also saves an actual main-menu multiplayer screenshot.
Offline autosaves must be rejected both during the session and after a local
disconnect while the multiplayer world is still loaded.
The test checks networking and simulation state; it does not measure latency
or prove all campaign scripts and physics interactions work correctly.

`-SteamHostOnly -Backend Steamworks` runs one actual game instance against the
configured Steam AppID. It starts a friends-only campaign lobby, verifies world
updates through menus, changes to Old Archives while retaining the same lobby,
checks save suppression, and leaves the lobby. It uses the signed-in Steam account
and sends no invitations. This tests Steam hosting integration, not a two-account
P2P gameplay connection. Test the latter on separate machines/accounts with access
to the title, using the in-game lobby code or a Steam invitation.

The UI process is limited to 20 seconds. Full-game processes have a 90-second
internal limit and a 110-second external limit, including cleanup on failure.
The full-game runner uses fresh `Documents/Amnesia/codex-multiplayer-smoke-*`
directories and deletes only the exact directories it created after copying
their logs. Use `-KeepProfiles` to retain those profiles for debugging. Retail
assets and existing profiles are unchanged; asset cache saving is disabled.
Artifacts and generated build files stay under `bld/multiplayer-tests/`.
