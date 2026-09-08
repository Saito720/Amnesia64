# HPL2 networking dependency

The engine's `network/NetworkTransport.h` supports two mutually exclusive build
backends. **Steamworks is the default when the SDK is present** and supplies
Steam lobbies, identities and Steam Datagram Relay; see the
[Steamworks integration instructions](../steamworks/README.md).
`/p:HplUseSteamworks=false` (Visual Studio) or `-DHPL_USE_STEAMWORKS=OFF` (CMake)
selects the open-source standalone backend described below. Both headers and
libraries switch together; never link the standalone GNS library and Steam API
into the same executable.

The standalone backend provides direct-IP, encrypted reliable/unreliable UDP
connections without Steam. Hosts must make their UDP listen port reachable
(default in Amnesia: 27015); this backend has no relay or matchmaking service.

When the standalone backend is selected, the solution builds these dependencies:

* [Valve GameNetworkingSockets v1.6.0](https://github.com/ValveSoftware/GameNetworkingSockets/tree/2cb93a06350bb065db53abdb0d87cf297e0bfd34),
  commit `2cb93a06350bb065db53abdb0d87cf297e0bfd34`, BSD-3-Clause.
  Original source archive SHA-256:
  `826a55aba46b421d1198851f1a70ef63dca27f0bf91db23730321ae2f92d6294`.
* [Google protobuf v21.12 / C++ 3.21.12](https://github.com/protocolbuffers/protobuf/tree/v21.12),
  BSD-3-Clause. Original source archive SHA-256:
  `22fdaf641b31655d4b2297f9981fa5203b2866f8332d3c6333f6b0107bb320de`.

Licenses are retained in each source directory, including the cryptographic
implementation notices under GameNetworkingSockets/src/external. Unused upstream
test fixtures, CI files and non-C++ protobuf language distributions are omitted.
The only upstream build modification is accepting an already-created
`protobuf::libprotobuf` target; the wrapper builds matching `protoc` and runtime
sources together. No network downloads occur while building.

Windows uses BCrypt and the bundled Curve25519/Ed25519 implementations, static
GNS/protobuf libraries and the existing game's dynamic MSVC runtime. Other
platforms use upstream's OpenSSL backend and require a system OpenSSL development
package. Direct connections are encrypted but not authenticated by Steam.

Build on Windows with Visual Studio 2026, the v145 C++ toolset and a CMake version
supporting the Visual Studio 18 2026 generator:

```powershell
./HPL2/dependencies/networking/Build.ps1 -Configuration Debug -Platform x64
```

The standalone test uses real loopback sockets and verifies connection lifecycle,
peer IDs, a fragmented 60 KB reliable payload, unreliable messages, capacity
rejection, disconnect reasons and restart after final-library-user teardown:

```powershell
cmake -S HPL2/dependencies/networking -B bld/networking/v145/x64 -G "Visual Studio 18 2026" -A x64 -T v145 -DHPL_USE_STEAMWORKS=OFF -DHPL_NETWORK_TESTS=ON
cmake --build bld/networking/v145/x64 --config Debug --target hpl_network_transport_test --parallel 8
ctest --test-dir bld/networking/v145/x64 -C Debug --output-on-failure
```

All wrapper calls run on the game's main thread. Messages are limited to 64 KiB;
large map transfers must be chunked and retry sends that return false when the
4 MiB connection queue is full. Polling is capped per frame. Peer 0 addresses the
host from a client, and host-assigned remote peer IDs start at 1.
