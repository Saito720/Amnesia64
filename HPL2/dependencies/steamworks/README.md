# Steamworks SDK 1.65

This is the Windows, Linux and macOS subset of the user-provided root
`steamworks_sdk_165.zip`. It contains the unmodified C++ public headers, Steam API
runtime/import libraries, and the original SDK release notes. Tools, samples,
encrypted application ticket libraries and Android binaries are omitted.

Original archive SHA-256:
`8c42792e09100988e31e3dc069de2eb1bc60702a0445bb37298ba0c54067c202`.
The retained `sdk/Readme.txt` identifies version 1.65, dated 23 July 2026.
Valve's original copyright notices are retained. These SDK files are governed by
your Steamworks SDK agreement; they are not covered by HPL2's GPL or the BSD
license of the separate open-source GameNetworkingSockets distribution. Consult
your Steamworks agreement before redistributing SDK development files. Ship only
the appropriate Steam API redistributable with the game.

## Build selection

Visual Studio automatically selects Steamworks when
`sdk/public/steam/steam_api.h` is present. `Networking.props` provides a single
shared backend selection for HPL2 and Lux in Debug/Release, x64/Win32. Steam builds
link the SDK's `steam_api64.lib` or `steam_api.lib`, never the standalone
GameNetworkingSockets or protobuf libraries. The SDK headers likewise replace
the standalone headers. The solution's standalone dependency project becomes a
no-op in this mode.

```powershell
MSBuild Amnesia.sln /t:Lux /p:Configuration=Debug /p:Platform=x64
# Account-free, direct-IP standalone backend:
MSBuild Amnesia.sln /t:Lux /p:Configuration=Debug /p:Platform=x64 /p:HplUseSteamworks=false
```

`HPL_STEAM_APP_ID` is compiled from the root `steam_appid.txt`; its current value
is `1362050`. Override with `/p:HplSteamAppId=your_app_id` when needed; the
development AppID file must match, or disable its deployment and launch through
the matching Steam app. All players
using Steam networking must be signed into Steam and have a license for this app.
An unpublished app can be tested using accounts granted access through its
Steamworks packages. The SDK does not grant ownership or configure your partner
backend.

The build copies the matching Steam API DLL and root development
`steam_appid.txt` beside the generated executable. It never deploys files into
the separate retail Amnesia asset directory. Development initialization uses the
executable's app ID context so a retail asset working directory cannot silently
select Amnesia's unrelated AppID.

**Do not include `steam_appid.txt` in a Steam depot release.** Set
`/p:HplDeploySteamAppId=false` for packaging builds and exclude any old copy left
in a reused output directory. Keep the Steam API DLL. When launched by Steam,
the app context is supplied by Steam itself.

The CMake equivalent is `-DHPL_USE_STEAMWORKS=ON` (the default when SDK headers
are present), with `HPL_STEAMWORKS_SDK`, `HPL_STEAM_APP_ID` and
`HPL_DEPLOY_STEAM_APP_ID` overrides. `OFF` retains the standalone source build.
The imported Steam API target selects Windows x86/x64, Linux x86/x64/arm64 or
macOS and deploys the matching runtime beside the game executable. Existing
non-Windows game toolchain dependencies remain required; the Windows build is
the verified platform.

For the multiplayer session behavior and remaining gameplay limitations, see
[`MULTIPLAYER.md`](../../../MULTIPLAYER.md). Steam lobby and relay functionality
uses Steam services; the player's machine remains the simulation host.
