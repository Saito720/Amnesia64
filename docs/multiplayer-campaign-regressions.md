# Multiplayer campaign regression checks

Use the same protocol-10 build on both peers. Keep **every player may trigger
Player callbacks** enabled for the client interaction tests.

## Changes under test

- Wheel snapshots include accumulated angle and stuck state. The current
  simulation owner updates the wheel; the host runs its gameplay connections.
  Rebase the hinge angle on ownership handoff so a network correction is not
  counted as another turn.
- Rope length and motor settings come from the host. Each attachment applies
  tension only on the peer responsible for simulating that body.
- Script interactions use a separate burst allowance instead of the map-change
  cooldown, and validate reach against body bounds and authored focus distance.
- Item-use and combination registrations are sent as current snapshots, including
  saved-map registrations. Clients request the host's registered callbacks;
  client packets never supply executable script text. Puzzle-item pickup ownership
  survives map transitions, and script removal clears that ownership.
- At client readiness, reconcile authored item removals against the restored host
  map. The client receives removals even when those pickups happened on an earlier
  visit.
- Insanity areas check the local player; insanity event start/stop commands are
  no longer broadcast. Scripted enable/disable settings still configure event sets.
- Live menu backgrounds draw player helpers, screen effects and local insanity
  overlays before the menu's dimming layer.

## Two-player playthrough checks

Also test `25_cell_tunnels.map`: leave the host at the spawn and have the client
use `level_dungeon_1` with client map changes enabled. Its authored `G:/...` map
reference must resolve to the same destination as a filename-only door. Repeat
with client map changes disabled and with the players' roles reversed.

1. In `17_control_room.map`, have the client turn each of the three left chain
   wheels through several revolutions in both directions. Both peers should see
   smooth weight movement and consistent puzzle completion. Release a wheel,
   let the other peer take over, and join a third peer after moving the weights.
2. In `03_archives.map`, click `PassageInteractArea` three times rapidly from the
   client, including near its configured interaction limit. The wall should break
   on both peers. Repeat as host; verify ordinary physics impacts still work.
3. Collect `key_study_1` as client, travel to `02_entrance_hall.map`, and use it on
   `level_wood_2`. Verify the door unlocks on both peers and the key is consumed.
   Repeat with the host collecting it. Also check a scripted inventory combination
   and item-use callbacks that intentionally remain registered after use.
4. With players separated, enter an insanity area on one peer. The other peer
   should not start that event or consume its own area's one-shot activation.
5. Collect items in `26_torture_nave_redux.map`, trigger the diary vision, and
   return from `26_zimmerman_vision.map`. Previously collected items, including
   the diary, should be absent on both peers. Repeat the return and a late join.
6. With low health and active screen effects, open/close the pause menu, inventory
   and journal on each peer. The red tint and other overlays should continue
   beneath menu darkening; the world should continue updating.

## Automated checks

Death recovery also accepts a new death during the respawn fade. Health is
restored before that fade finishes; previously a lethal hit in this interval
was ignored by the death helper and left character input blocked by zero health
with no further respawn scheduled. This is independent of which menu is open.
In cell tunnels, the guardian checkpoint overlaps `AreaGuardianKill_lump_2`,
and multiplayer does not reset the shared guardian sequence on local respawn.
Verify on host and client: die to the guardian, open/close pause during recovery,
and verify a second lethal hit starts another death/respawn rather than leaving
a stationary character. Also check recovery without another hit, and ordinary
enemy damage during the respawn fade. Persistent hazards may legitimately kill
the player again; this change does not disable them or reset shared scripts.

Build `Amnesia.sln` with target `Lux`, configuration `Debug`, platform `x64` before
running `tests/RunMultiplayerWorldTests.ps1`; that runner links `x64/Debug/HPL2.lib`.
Building the game project directly uses a different output directory.

`tests/RunMultiplayerProtocolTests.ps1` covers packet bounds, truncation, invalid
wheel states, rope state round trips and non-finite values. The Newton smoke suite
also covers multi-turn wheel initialization, owner relay, stale updates, handoff,
and disabling/restoring rope tension, alongside existing physics regressions.

The automated tests do not replace the two-player campaign and visual checks above.
