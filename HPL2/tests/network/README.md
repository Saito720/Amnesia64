# Transport regression coverage

`RunSteamTransportTests.ps1` builds the Steam metadata, lobby lifecycle, and
direct-IP transport tests. Add `-Live` to run the tests against the signed-in
Steam runtime. The lobby lifecycle test creates only a temporary friends-only
lobby and sends no invitations.

The loopback test verifies that a host-initiated disconnect leaves the listener
running, permits a fresh connection without restarting the host, assigns a new
peer ID, rejects sends to the retired ID, and delivers reliable traffic over the
replacement connection. It does not authenticate two different Steam accounts.

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
