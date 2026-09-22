# Transport regression coverage

`RunSteamTransportTests.ps1` runs the Steam metadata and deterministic transport
ordering tests, and builds the lobby lifecycle and direct-IP transport tests.
Add `-Live` to run the latter tests against the signed-in
Steam runtime. The lobby lifecycle test creates only a temporary friends-only
lobby and sends no invitations.

The loopback test verifies that a host-initiated disconnect leaves the listener
running, permits a fresh connection without restarting the host, assigns a new
peer ID, rejects sends to the retired ID, and delivers reliable traffic over the
replacement connection. It does not authenticate two different Steam accounts.

The deterministic test drives the production standalone transport with a small
SDK substitute. It holds back Connected callbacks while reliable messages arrive,
then verifies that Connected reaches the game before Message, exactly once, on
both sides. It also checks receive-group setup failure, bounded send diagnostics,
expected unreliable NoDelay drops, and draining diagnostics without consuming
queued disconnect events. The Steam backend follows the same receive-group
ordering; the live loopback test exercises that backend with the real SDK.

For an authenticated retry check, use two Steam accounts:

1. Host a friends-only session and join it from the other account.
2. Reproduce a host-side initialization rejection, such as a test build returning
   failure from `SendMapBaseline` after the client acknowledges the map.
3. Keep the host session running and retry from the same account. The retry must
   reach the map handshake again. If the initialization fault is still present,
   the same initialization reason is expected; the account must not instead be
   rejected as a nonmember of the lobby.
4. Remove the initialization fault or change to a supported map without restarting
   the host, then join successfully using the same lobby ID.

Ordinary disconnects do not ban Steam identities. Each new connection must still
pass the original-host, protocol, lobby-membership, duplicate-connection, and
capacity checks.

## Diagnosing a stalled join

Keep `hpl.log` from both players before launching the game again. Multiplayer
diagnostics include UTC timestamps and application elapsed milliseconds so the
two logs can be compared (allow for differences between the computers' clocks).
They record Steam connection transitions and native end codes, the first received
message, and the game's `Hello`, `MapBegin`, `MapRequest`, `MapEnd`, `Ready`, and
initial world-state milestones. Send acceptance means queued locally, not
confirmed delivery to the other player.

While a player is joining, each side records its current wait state at most once
every ten seconds. A host record with `greeted=0` means the game has not accepted
`Hello`; `manifest=1 request=0` means it has queued the map information and is
waiting for the client's request. A client at epoch zero in
`waiting-for-host-map` has not accepted a map manifest yet. A
`discarded packet without a registered game peer` record, together with the
transport's `connected_event_queued` field, helps identify early-message ordering
problems. Receive-group attachment now waits for the validated Connected callback
so early messages stay buffered until the game can register the peer. A failed
initial Hello send ends the join immediately with a reason. Protocol version,
packet layouts, and timeout values are unchanged.

See [multiplayer diagnostics](../../../docs/multiplayer-diagnostics.md) for the
other recorded events, log limits, and remaining two-account checks.
