# Multiplayer campaign regression checks

Use the same protocol-14 build on both peers. Keep **every player may trigger
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
wheel states, rope state round trips, entity reconstruction and attachment order,
inventory transfer/recipe selection, trigger identity and non-finite values.
The Newton smoke suite also covers multi-turn wheel initialization, owner relay,
unlock-before-handoff for wheels/levers/sliders, sticky detachment, departed-player
poses, replacement-body generations, rotated trigger shapes and disabling/restoring
rope tension, alongside existing physics regressions.

The automated tests do not replace the two-player campaign and visual checks above.

## Multiplayer reliability follow-up

This change addresses review items 1, 3–16, 18 and 19. Historical one-shot script
replay (2), ordinary critter AI (17), and shared checkpoint recovery (20) remain
outside this change. All peers must upgrade together because physics generations,
player query values, inventory transfers and entity definitions change the wire format.

- Disconnect a client carrying a progression key after moving to another map.
  The host should receive that inventory entry, without a second world pickup
  callback. Tinderboxes, oil, sanity/health potions and lanterns must not transfer.
  Notes/diaries and coins are consumed into journal/currency state rather than held
  inventory entries, so they are not transferred as physical items.
- Split the three storage drill parts across three players. The player collecting
  the final part should receive the drill, with all parts removed from their
  actual holders. Repeat a two-part recipe with the host collecting last, and
  verify an incomplete conditional recipe remains usable. Picking up an unrelated
  item must not claim another player's crafting result.
  Keep an ingredient equipped in UseItem while another player completes its recipe;
  consuming it must safely return its owner to normal interaction. Separate recipes
  using one generic callback must not claim unrelated crafting results.
  Combine ingredients manually as host as well; consuming both items must leave
  the output in the host's inventory and remove an auto-destroy recipe safely.
- Spawn a puzzle prop through a script, leave the map, and return. Verify the
  restored prop exists with matching physics and native state on clients. Repeat
  with a replacement resource, a late join, and props attached to other props
  (such as a bucket on a rope). Attachments must retain their parent and offset,
  including when the parent is asleep. Definitions are sent before state;
  this does not replay an earlier visit's scripts.
  Include a moved prop saved with static physics and a same-resource replacement
  whose earlier incarnation had broken joints. Deleting an attached parent during
  a join must not invalidate the reconstruction roster.
- Have only the client look at a scripted entity. Verify visibility, distance and
  obstruction checks, both entry/exit callback identities, and the host-only
  player-trigger option. Repeat while the host looks away behind a wall.
- Use an area-exit callback and a timer callback that query player health, sanity,
  oil, position, velocity and item ownership. The triggering client should remain
  the query subject. Chain another timer from the first one and disconnect before
  it fires; missing clients must not silently become the host for these getters.
  Stop and rehost the current map before an old remote timer fires. A newly joined
  player reusing its peer number must not inherit that timer or a remembered exit.
- Grab a detachable sticky-area object from the client, including an object
  restored already attached on a map revisit. Both peers must detach it and agree
  on mass/gravity. A non-detachable attachment must remain locked.
- Test interaction-unlocked wheels, levers and multi-sliders from the client.
  Repeat ownership handoff and verify the unlocked limits reach both machines.
- Enter slime as the client while the host stands elsewhere. Only the client
  should lose health and receive screen shake; authored callbacks retain the
  triggering player and shared effects still originate from the host.
- Toggle a native button and buy a locked chest as either player. Repeat simultaneous
  attempts; only the successful buyer pays, the unlock/toggle state converges, and
  callbacks run once on the host. Cancel the chest confirmation without paying.
  Replace or destroy a chest while its purchase question is open; the question must
  close safely without charging the player or calling a destroyed chest.
- Disable **every player may trigger Player callbacks**, then collect a scripted
  diary as client. Its accepted pickup/diary completion callback must still run.
  The separate **allow clients to trigger map changes** permission still applies.
- In a custom map, use a level door whose destination is another start position
  in the same map. A client use should fade/relocate that client, leave the host
  in place, and preserve the world. Different-map doors still transition everyone.
- Inject a delayed player pose after disconnect and an old body snapshot after
  replacing an object. Neither the departed player nor the old object state should
  return. New incarnations must accept their own snapshots normally.
- Walk beside an angled thin script area (for example `AreaHelpMe` in prison north).
  The client must enter the actual shape before its callback runs, matching host
  detection rather than the larger axis-aligned bounding box.

## Duplicate names and failed-join recovery

- Host the unchanged `09_back_hall.map`, join, and revisit it after a map change.
  Both instances of `Even01Slime04` (IDs 1030 and 1040) and `Even01Slime02_3`
  (IDs 1047 and 1055) must remain distinct. Reconstruction, attachments, native
  state, and interactions use entity IDs so repeated names do not reject a map
  or overwrite a sibling prop.
- After a host-side initialization rejection, retry from the same Steam account
  without restarting the host. A transient disconnect must not ban that account
  from the still-valid lobby. Normal membership and identity checks still apply.
- Check both instances' `hpl.log` when initialization fails. Session lifecycle
  messages identify the role, map, epoch, peer, and reason; reconstruction failures
  additionally identify the entity and failed operation. The rejection status
  carries the specific reconstruction error instead of only a generic failure.

## Enemy-damaged doors and native debris

- In `10_daniels_room.map`, let the Grunt damage and destroy the crowbar door
  (`mansion_1`). Both peers must show each damage stage, then hide the broken
  leaf while retaining the hinges. The client must not retain a visible door
  over its disabled collision body.
- Debris must appear once on each peer and disappear after its authored lifetime
  (four seconds for this door). Reconnect and visit Study and back: the door
  remains broken and expired debris must not reappear.
- Use scripted health changes and `ResetProp` on a breakable door, including two
  breaks around a reset within one update. Check matching damage meshes, one
  surviving debris instance, and exactly one sound/particle pair per transition.
  Break callbacks run on the host; any props they create must reach the client.
  Resetting a door after debris expires must not delete another prop reusing its ID.
- Break an object whose destruction callback creates and configures another prop.
  The client must retain one copy with the host's ID and configured state. An
  early local destruction must not let that callback reuse the source's ID while
  the host still holds it during the callback.
