# Multiplayer diagnostics

Save both players' `hpl.log` files before relaunching the game. Include the build,
map, approximate time, and what each player was doing. In particular, distinguish
an established connection dropping from a later reconnect waiting for the map.

Multiplayer records include UTC time and application elapsed milliseconds. Game
records also identify the role, map, and epoch where applicable. Allow for clock
differences between computers; peer and connection IDs are local to each process.
The log records resource names where relevant, without dumping packet payloads
or complete script commands.

## Recorded events

| Area | Useful context |
| --- | --- |
| Session setup | Transport/backend, protocol, player limit, relevant host settings, startup and map-preparation failures. |
| Transport | Connection transitions and native end codes, acceptance/rejection, first received message, first unexpected SDK send failure, disconnect queue/quality information when available. |
| Join and map transfer | Hello, manifest, request, transfer completion, Ready, initial-state setup, local/cache/downloaded map selection, preparation cancellation, and periodic wait state. |
| Replication | Initial-state counts and completion, missing or ambiguous body/entity/rope mappings, enemy creation waits and recovery, incompatible native state/resources. |
| Interactions | User interaction ownership grants/releases, denied requests, response timeouts, expired or invalidated ownership, inventory recovery. |
| Scripts and effects | Rejected script replication, malformed command location, effect resource/replay failures, budget failures, and late joins disabled by replay or synchronization limits. |

"Sent" or send acceptance means queued locally; it does not prove remote delivery.
An unavailable SDK health query is recorded explicitly instead of reporting zero
latency or empty queues. The ordinary Warning severity is retained where an
existing warning gains the additional context.

## Keeping the log useful

- Joining peers report their wait state at most every ten seconds. Established
  sessions do not produce periodic heartbeat or per-packet logs.
- Repeated mapping, resource, and request anomalies share fixed categories. Each
  limited category records its first eight occurrences per map, then one
  suppression notice. Map changes and session shutdown summarize further counts.
- Transport send failures are reported once per connection. Expected unreliable
  NoDelay drops remain quiet.
- Successful snapshots, ordinary physics contact ownership changes, and normal
  script commands remain quiet. User interaction ownership changes are recorded
  to help correlate puzzle actions with failures.
- Log text is bounded and control characters are flattened to keep each record on
  one line. Mapping-recovery tracking and unregistered-peer diagnostics also have
  fixed memory bounds.

## Interpreting a stalled join

The [transport test notes](../HPL2/tests/network/README.md#diagnosing-a-stalled-join)
explain the handshake flags. A client waiting at epoch zero has not accepted a
map manifest. Compare the host's Hello/manifest records before attributing that
wait to loading speed or network instability.

The review corrected a message-ordering gap in both transport backends: messages
could previously be drained before the asynchronous Connected callback registered
the game peer. Receive-group attachment now follows the validated callback.
Steamworks documents that attaching a group preserves already queued messages
in [SetConnectionPollGroup](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets#SetConnectionPollGroup).
A deterministic regression reproduces the old failure; this does not establish
that it caused any particular playthrough incident.

The initial Hello now fails promptly if the SDK cannot queue it. Map validation
also checks each dependency before adding it to the 64 MiB budget, preventing the
last asset from exceeding that existing limit. Wire layouts, protocol version,
and timeout settings are unchanged.

Multiplayer warnings now pass through the bounded formatter, including long map
rejection reasons and optional sound-preload errors. This avoids the engine's
legacy fixed-buffer warning formatter receiving an oversized string. Stale map
acknowledgements use the limited handshake category rather than logging every
ignored packet.

## Coverage and remaining manual checks

Automated coverage includes delayed connection callbacks, disconnect/reconnect,
send-failure logging limits, diagnostic preservation on shutdown, and dependency
budget boundaries. Existing protocol, content, native physics, and full-game
smoke runners cover the surrounding replication paths.

Loopback tests do not reproduce two accounts communicating through Steam Datagram
Relay. With two accounts, test an ordinary join, a reconnect to the same running
host, a join during map preparation/loading, and a temporary network interruption.
Save both logs after a failure before restarting either game. The authenticated
rejection/retry procedure is in the transport test notes. These checks also help
distinguish connection loss from a separate map-handshake failure.
