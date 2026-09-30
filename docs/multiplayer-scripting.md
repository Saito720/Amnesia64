# Multiplayer scripting, Version 2

Version 2 is an opt-in scripting layer. Authority scripts make world and gameplay decisions. A separate client module provides local presentation and update code for each player, including the hosting player. An unmodified map without the opt-in file keeps legacy behavior.

This is the master reference for the implemented system. The [searchable HTML edition](multiplayer-scripting.html) contains the same guide and a filterable catalog of every registered native. The catalog below lists the original 260 names and 15 new helper names, including all overloads and the exact 64 names available in client companions. Earlier proposed scope tables have been retired; their suggested routing was not the implemented contract.

Here, shared means one authority execution with supported consequences synchronized by the existing native-effect system. It does not execute the authority function independently on every machine. A selected-player operation targets that character's controller or account. The host authorizes the trigger and dispatches the relevant local work, including to its own separate client instance. `Server` and `Client` prefixes identify module lifecycle hooks; they do not change arbitrary function or native ownership.

## Files and opt-in

For a map named `hallway.map`, place these files in the same directory:

| File | Purpose |
|---|---|
| `hallway.scriptcfg` | Selects Version 2 for this map. |
| `hallway.hps` | Normal authority map script. |
| `hallway.client.hps` | Optional client map script. |
| `global.client.hps` | Optional client companion for the global script. |
| `inventory.client.hps` | Optional client companion for the inventory script. |

The complete configuration is:

```ini
[Scripting]
Version=2
```

The host reads client companion sources, validates and compiles them, and sends a hashed package in memory. Clients do not look for replacement `.client.hps` files on their own disk. No client script cache is written. Received source remains inspectable by someone controlling the receiving machine; this is not code secrecy or copy protection.

A module is limited to 96 KiB of source and the package to 256 KiB. The manifest is limited to 4096 bytes, permits comments and whitespace, and requires exactly one `[Scripting]` `Version=2` assignment; additional settings are rejected. An absent manifest selects legacy behavior; an invalid manifest is an error, not a silent legacy fallback. Optional absent companions simply contribute no client module.

This first client VM supports primitive values, strings, functions and module globals. Script classes and interfaces are disabled in the restricted VM; use plain values and functions. A malformed manifest, forbidden native, invalid hook signature or conflicting alias prevents the package from being accepted. Client globals are compiled without executing their initializers and initialized only after the readiness barrier. Entry execution has a bounded line-callback budget, described below.

## Lifecycle and readiness

In authority modules, `OnStart` and `ServerOnStart` are aliases for the same hook. Define one. Defining both is an error, rather than two calls. The same rule applies to `OnEnter`, `OnLeave`, `OnGameStart`, `OnUpdate` and `OnPlayerReady`. Hooks must have their exact signatures.

| Authority hook | Client counterpart | Lifetime |
|---|---|---|
| `void ServerOnStart()` | `void ClientOnStart()` | Authority first-visit setup; fresh client map instance setup. |
| `void ServerOnEnter()` | `void ClientOnEnter()` | Enter the corresponding map instance. Client entry follows baseline installation. |
| `void ServerOnLeave()` | `void ClientOnLeave()` | Leave the corresponding ready map instance. |
| `void ServerOnGameStart()` | `void ClientOnGameStart()` | Global/inventory module initialization. Retained client instances run this once. |
| `void ServerOnUpdate(float step)` | `void ClientOnUpdate(float step)` | Local map update. No per-tick network invocation. |
| `void ServerOnPlayerReady(int player)` | No automatic counterpart | Authority receives an initialized, available player and an attributed player context. Runs for the hosting player and late joins. |

Unprefixed hooks in a client companion are also aliases for its `Client` hooks. Prefer explicit prefixes to make the file's role clear. Authority and client hooks use separate AngelScript globals; a listen server does not share their variable storage.

Authority map initialization may run before clients are ready. Put personal setup that calls `RunClientCallback` in `ServerOnPlayerReady`, or respond to a later attributed event. The remote ready hook waits for both the script initialization acknowledgement and an available player pose. It runs once per participant for that map/session, not again on character respawn. Turning an already loaded offline map into a hosted session creates a new session; the local ready event intentionally runs again. A client runs its globals and initial hooks only after the queued entity, physics-body and enemy baselines and its initial published snapshot are installed. Map `ClientOnStart` runs for each fresh instance, including revisits; it is not the authority first-visit test.

Authority initializers run during module construction and have no implicit actor. Their context captures the current session/map identity, including hot recompilation, but this does not make map resources available earlier. The initial global script and its game-start hook can precede map loading. `AddTimer` uses the current map and cannot arm a map timer before one exists; put map-dependent setup in map lifecycle or player-ready hooks.

A remote participant's first player-ready dispatch requires a living pose no older than two seconds. A dead participant remains enumerable but waits for a suitable pose before that initial ready notification. Once notified, respawn does not run the hook again.

If a level-door map load fails, the current client instances, timers, player variables, publications, completion bindings and readiness bookkeeping are restored without rerunning entry hooks. Disconnects during that attempt also prune the rollback copy, so cancellation cannot resurrect a departed participant's private state. Authored `OnLeave` side effects, such as world changes or assignments to script globals, are not undone. On successful replacement, directed cleanup and typed events received during preparation are applied to the outgoing ready instance before it is replaced. During multiplayer, actor-bound timers use character-life IDs that remain monotonic across map resets, so revisiting a saved map cannot revive delayed work from before a respawn.

## A map callback and its client companion

Create a script area named `welcome_area` in the editor. In `hallway.hps`:

```cpp
void ServerOnStart()
{
    AddPlayerCollideCallback("welcome_area", "PlayerEntered", true, 1);
}

void ServerOnPlayerReady(int player)
{
    // This callback already selects player. Campaign values survive map changes.
    PublishPlayerVar("welcome_text", "A new hallway awaits.");
}

void PlayerEntered(string &in parent, string &in child, int state)
{
    // The triggering player is selected automatically.
    int visits = GetPlayerVarInt("area_visits", true);
    SetPlayerVarInt("area_visits", visits + 1, true);
    RunClientCallback("ShowWelcome", "welcome_text");
}
```

In `hallway.client.hps`:

```cpp
float elapsed = 0.0f;

void ClientOnEnter()
{
    elapsed = 0.0f;
}

void ClientOnUpdate(float step)
{
    elapsed += step; // This is private local state; ticks are not sent over the network.
}

void ShowWelcome(string &in publishedName)
{
    string message = GetPublishedScriptVar(publishedName);
    AddDebugMessage(message, false);
    FadeSepiaColorTo(0.4f, 1.0f);
    AddTimer("welcome_end", 2.0f, "EndWelcome");
}

void EndWelcome(string &in timerName)
{
    FadeSepiaColorTo(0.0f, 1.0f);
}
```

`RunClientCallback` passes its string as a typed argument, not as executable script text. It selects the corresponding module (`map`, `global` or `inventory`) on the intended player's machine. The client function must be `void Function(string &in)`. There is no general client-to-host script evaluator.

## Player selection and world operations

An attributed callback supplies its initiating player. `GetScriptPlayerId()` returns that ID; it returns `-1` in a Version 2 world context without a player. `SelectScriptPlayer(int)` changes the player for the rest of the current dispatch. Nested dispatch restores the caller's context when it returns.

Authority world hooks such as `ServerOnStart` and `ServerOnUpdate`, and authority global initializers, have no implicit player. They may edit shared entities directly. To apply a personal operation from a world hook, select a player explicitly. `GetScriptPlayerCount()` and `GetScriptPlayerIdAt(int index)` enumerate the local player first, then remote players with available poses in ID order; this includes dead participants. An invalid index returns `-1`. Enumeration is authority-only. Never store an index as a permanent player identity. Selection validates availability but does not promise a living character or an initialized remote client module.

Player IDs identify participants in the current session, separately from character-life IDs used to reject obsolete work after respawn. They are not Steam account IDs. This implementation does not add script accessors for Steam usernames/accounts; selected achievement delivery uses the existing account subsystem. Reconnecting creates a new participant rather than attaching old private state by account.

Player health, sanity, oil, position, speed and lantern queries use the selected actor. Directed health/sanity/oil and position changes update authority state and reconcile with the owning client. Movement and presentation execute through the owning controller or local subsystem. A missing participant is not silently replaced by the host player. Lantern state is the owning player's latest received snapshot; a new owner command is not an immediate synchronous lantern acknowledgement. Reads of remote presentation state without a replicated value fail explicitly; client companions can query their own presentation state.

Pending authority vitals and teleport changes overlay stale owner poses. Forwarded poses include that overlay until the directed revision is acknowledged and a newer pose retires it. Revival advances the authority character-life identity immediately; an older-life pose cannot undo revival. These rules cover supported mutations, not an arbitrary private physical world per player.

Selecting a player does not redirect shared-world APIs. A callback can unlock one shared door and show a message to just its player. Journal/quest natives and shared checkpoint behavior retain their existing shared policy. Offline autosave remains suppressed during multiplayer. This version does not add personal journals, personal checkpoint records or a multiplayer disk-save format.

`HasItem` requires a selected actor in Version 2. Remote inventory queries use the authoritative item ledger, which tracks supported progression/hand-object ownership; they are not a complete mirror of every locally consumed health, sanity, oil or tinderbox item. Do not use this API as a synchronized remote consumable-count query.

`UnlockAchievement` with a selected actor targets that account. Without an actor it sends a transient award to the current host and initialized remote participants. These awards are not late-join replay history, and initial state replay cannot grant them again. The transport and eligibility rules are tested without issuing live Steam achievement awards.

## Variables and published snapshots

The legacy `LocalVar` functions still mean map lifetime, and `GlobalVar` functions still mean cross-map story lifetime. They remain authoritative and are not client VM bindings.

Explicit player variables are separate:

```cpp
void SetPlayerVarInt(string &in name, int value);
void SetPlayerVarInt(string &in name, int value, bool campaign);
void SetPlayerVarFloat(string &in name, float value);
void SetPlayerVarFloat(string &in name, float value, bool campaign);
void SetPlayerVarString(string &in name, string &in value);
void SetPlayerVarString(string &in name, string &in value, bool campaign);
int GetPlayerVarInt(string &in name);
int GetPlayerVarInt(string &in name, bool campaign);
float GetPlayerVarFloat(string &in name);
float GetPlayerVarFloat(string &in name, bool campaign);
string@ GetPlayerVarString(string &in name);
string@ GetPlayerVarString(string &in name, bool campaign);
void PublishPlayerVar(string &in name, string &in value);
string@ GetPublishedScriptVar(string &in name);
```

Player-variable setters and getters are authority-only and require a selected player. Names are isolated by module and player. The short overload uses current-map lifetime; passing `true` as the final argument retains the value across maps in the same session. These are actual overloads because this AngelScript version does not provide default arguments. Respawn retains these variables. Disconnect removes the participant's values. Starting another session clears the store. These values are not saved to disk and do not follow an account through reconnect.

Missing int/float variables return zero; missing strings return an empty string. Reading incompatible text through a numeric getter raises a script error, and float values must be finite. Names are limited to 128 bytes and strings to 4096 bytes. The private store is bounded to 4096 entries and 4 MiB. Each string getter returns a fresh string object (`string@`); editing it cannot mutate the stored value.

Client `StringSub` also returns a fresh `string@`, while authority keeps its legacy `string&` declaration. A negative or out-of-range start returns an empty string, a negative count selects the remainder, and a positive count is clamped to the remaining length without signed overflow. Legacy authority `GetLocalVarString`/`GetGlobalVarString` retain their mutable-reference semantics; they are not registered in the restricted client engine.

`PublishPlayerVar` publishes an explicit string to the selected player's read view. It does not automatically expose the private variable store. A snapshot contains only that player's published values, grouped by module. `GetPublishedScriptVar` returns a copy in the corresponding client module. Snapshots are replaced as a whole, are installed before initial client hooks, and are sent reliably when published values change. Each snapshot permits at most 64 values and 60 KiB including its value framing. Map-module publications end at map replacement; global/inventory publications last for the participant's session. Clients have no publication setter.

## Collisions, timers and completion callbacks

`AddEntityCollideCallback("Player", ...)` preserves combined player occupancy: one first entrance and one final exit. Its remove-after-collision flag removes the registration for everyone. Editor-authored callbacks continue to use authority dispatch.

`AddPlayerCollideCallback(child, function, oncePerPlayer, states)` adds a separate per-player observer. Its arguments remain `(string &in parent, string &in child, int state)`, with parent `"Player"`; use `GetScriptPlayerId()` for identity. States are enter `1`, leave `-1`, or either `0`. There is one explicit observer slot per child; registering with a player selected does not restrict it to that player. `oncePerPlayer` consumes the first matching event for that participant in the session, survives respawn and is independent of other participants. Death/disconnect clears obsolete overlap without inventing a personal leave. Before each sampled dispatch, the actor's life and availability are checked again. Removal and replacement during a callback apply to the appropriate live registration. `RemovePlayerCollideCallback(child)` removes the explicit observer, leaving the combined observer alone. The existing session setting controlling remote script triggers still applies.

Authority timers retain module, actor, session and character life. During multiplayer, a delayed actor callback is discarded after disconnect or respawn. Timers restored with a saved map rebind to that map's current transport generation. Client timers use a separate per-module namespace and a typed `void Function(string &in)` callback. A client map timer ends at map replacement; retained global/inventory client timers continue with their retained module. Replacing a client timer by name replaces that module's earlier timer.

This module capture applies to authority timers, collision registrations and completion tokens. Existing entity look/interact/connection and use-item callback strings continue through their established map handlers; inventory combination callbacks remain inventory callbacks. Editor-authored entries run on authority and do not gain new shared/server/client editor controls. There is no general callback registration redesign implicit in opting in.

Player look-at, voice-completion and lantern callbacks use host-issued completion tokens in Version 2. The host retains the registering module, selected player, session/map generation and character life. A client may report only its issued token; it cannot nominate a server function. Look-at and voice completion are one-shot: the token is consumed after the matching completion. Register another voice callback for another queue. Lantern bindings remain persistent until replaced, cleared or invalidated. Old tokens are invalidated with their participant or owning world. Empty look-at callbacks perform no callback work; the legacy look-at callback behavior remains unchanged.

## Transport and validation

Session/lobby protocol **16** is required on all peers; its Steam compatibility tag is `amnesia-hpl2-16`. New wire messages are `ScriptPackage` (45), `ScriptInitialized` (46), `ClientScriptEvent` (47), `PlayerScriptCommand` (48), `PlayerScriptAck` (49), `ScriptCompletion` (50) and `PublishedScriptState` (51). They use explicit bounded framing and nonzero map generations, rather than native structure serialization. All seven use reliable transport; individual messages remain within the transport's 64 KiB limit.

The host sends API-version-2 companion source for only `map`, `global` and `inventory` in reliable fragments of at most 32 KiB, with total byte count, sequential offset, map epoch and SHA-256 identity. The 256 KiB package limit includes its framing. The receiver rejects oversized, truncated, corrupt, duplicate and out-of-order package traffic; valid old-generation traffic cannot initialize the new world. An explicitly hashed empty package selects legacy scripting. This package is distinct from the persistent downloaded map resource cache and contains no authority `.hps` source.

Map-loaded readiness is an internal stage. Public readiness waits for script initialization. The host drains deferred entity/body/enemy baseline queues, then sends the player's current whole publication and an initialization marker. The client initializes globals and lifecycle hooks, then acknowledges that marker. Publications changed while waiting for the acknowledgement are refreshed before authority player-ready work. This prevents initializers from observing an incomplete baseline or a join-time write from being lost.

Directed player commands use one shared positive native-command allowlist on sender and receiver, include a revision, and preserve the existing per-resource fallback rules. They do not enter shared effect replay history. Client events specify a validated module, identifier and typed string arguments; public `RunClientCallback` supplies one string argument. Completion traffic contains issued tokens and a monotonically increasing client sequence, never an authority callback expression. The host checks peer, generation, session, module, character life, argument kind and one-shot/persistent token policy. Unauthorized, malformed and stale traffic is rejected or discarded according to the specific codec. This is not a network-facing script evaluator.

Event identifiers use ASCII letters/digits/underscore, cannot start with a digit, and are at most 128 bytes. Internal typed events allow zero to four strings, each at most 4096 bytes and without NUL. Completion bindings are limited to 256 total, with one current binding per player/kind across modules; replacing or clearing one removes the preceding binding. Tokens and sequence counters skip zero and are not rolled back. Publication limits apply across all three modules for one participant. There is no separate per-second scripting event/publication throttle; authors should publish on change, not every update.

Initialization has a 120-second timeout, including baseline delivery and acknowledgement. During map preparation, incoming old-world state is bounded to 32 MiB and 65,536 deferred packets for cancellation or outgoing-instance replay. Legacy shared effect history remains capped at 256 KiB per map; exceeding it disables late joins until the next map capture. None of these limits synchronize script update ticks.

## Execution limits and failures

Each Version 2 entry has a budget of **10,000 AngelScript line callbacks**: authority map/global/inventory initialization, lifecycle and trusted callback expressions; client initialization, lifecycle, events and timers. Nested entries share the remaining allowances of active ancestors and have a maximum depth of **64** while a bounded entry is active. A native callback cannot reset the outer budget by entering another script. Top-level legacy zero-budget execution retains its previous behavior. Client timers are limited to 256 outstanding timers across modules and 64 delivered callbacks per update. Names must be nonempty, NUL-free and at most 128 bytes; delays must be finite and between zero and 86,400 seconds. Update steps must be finite and between zero and one second.

These are execution guards, not CPU-time, compiler-time, heap or native-call limits. The authority engine remains trusted. AngelScript 2.19 authority script destructors and private object-factory contexts remain outside the watchdog; client classes/interfaces are rejected before initialization to remove that path from the restricted engine. Source size and positive native registration constrain the client surface but do not make the whole game a general-purpose untrusted-code sandbox.

An authority `OnUpdate` fault disables that hook until package installation or successful authority validation/recompile. Failed client startup, timer or update execution disables that client package generation; repeated initialization attempts do not clear the failure. Package installation or runtime reset clears it, and cancelled map transitions restore the preceding generation's failure/readiness state. Diagnostics retain the module, function and source line before a watchdog abort. Ordinary authority callback failures are reported to the host.

## Lifetimes and current boundaries

| State | Map replacement/revisit | Respawn | Disconnect/new session | Disk save |
|---|---|---|---|---|
| Authority LocalVar/GlobalVar | Existing map/campaign story policy | Existing native policy | Existing native story policy | Existing offline policy |
| Client map globals/timers | Fresh instance; old timers end | Retained | Runtime reset when leaving/resetting | None |
| Unchanged client global/inventory globals/timers | Retained; initialization once | Retained | Runtime reset or module replacement | None |
| Map player variables | Cleared for the new map instance | Retained | Removed/reset | None |
| Campaign player variables | Retained within participant session | Retained | Removed/reset; no account reattachment | None |
| Map publications | Cleared at map replacement | Retained | Removed/reset | None |
| Global/inventory publications | Retained within participant session | Retained | Removed/reset | None |
| Actor-bound authority timers/completions | Timers rebind only on legitimate saved-map restoration; world completion bindings expire | Multiplayer old-life work invalid; offline existing callback behavior | Removed/reset | No new persistence format |

- The client VM exposes only its allowlisted local presentation, utility and read APIs. Shared entity/AI/inventory mutation and saving are unavailable there.
- Client map globals and timers are fresh on map replacement; unchanged global/inventory companions retain state until a game/runtime reset or module replacement. A host continuing offline in the same world keeps its client instances. Private client state has no disk persistence.
- Published values are explicit copied snapshots. Full story-store replication and stable account-backed reconnect persistence are outside this implementation.
- Per-player collision observers are explicit. Other editor and native callback families retain their established registration/removal policy unless explicitly described above.
- Shared rendering changes such as entity activity, lighting and physics remain shared. This version does not introduce private physical-world overrides.

## Implementation and verification

`LuxScriptRuntime` owns the separate client engine, manifest/package validation, lifecycle dispatch, failure latches, typed calls and private player store. `LuxScriptExecution` provides scoped domain/module/actor ownership and per-player collision state. `LuxScriptPlayerState` implements bounded private storage and copied publications. `LuxScriptPackageProtocol` implements fragment validation. `LuxMultiplayer` coordinates package transfer, readiness, directed commands, completions and map rollback; `LuxMultiplayerWorld` reconciles supported actor state. `LuxScriptHandler` registers both native surfaces and routes supported personal operations. The engine `iScript`/`cSqScript` abstraction provides in-memory source loading, typed arguments, deferred globals and bounded execution; AngelScript's global reset accepts the supplied execution context.

The optional initialization budget preserves existing C++ source defaults but changes the engine script ABI. Rebuild the engine, game and other dependents together; do not mix an older AngelScript/HPL2 library with revised headers. Both Visual Studio and CMake source lists include the new runtime.

Run `./tests/RunMultiplayerProtocolTests.ps1` for package, context, per-player observer and player-state/snapshot checks. `./tests/RunScriptCoreTests.ps1` checks typed calls and VM limits. The multiplayer fixture runner's `-ScriptsOnly` mode exercises the opt-in layer in two local game instances; see [the multiplayer test guide](../tests/multiplayer/README.md) for build/backend requirements. Passing local fixtures does not claim cross-account relay or full campaign coverage for newly authored scripts.

The 2026-09-30 implementation review passed Debug/Release x64 builds and focused/full two-instance runs with both Standalone and Steamworks backends, all six protocol suites, core VM tests and production Newton checks. Coverage includes baseline backpressure before initialization, join-time publications, nested and cross-engine execution limits, online initializer ownership, copied native strings, callback replacement, actor-state reconciliation and cancelled-map rollback. Exact run IDs and the earlier unreproduced map-persistence assertion are recorded in the test guide. Separate-account Steam relay gameplay and live achievement awards remain untested.

<!-- BEGIN GENERATED NATIVE API -->

## Complete registered native API

This catalog is generated from the current registrations and client capability list: **275 names**, **281 authority declarations**, and **64 client names**. Declarations are copied verbatim from code. They are signatures for functions scripts call, not lifecycle functions scripts implement.

Every listed native is registered in the authority engine. Registration does not guarantee complete multiplayer replication of an operation. The handling column records implemented routing; names without directed routing retain their existing native handling. **Client: Yes** means the separate client engine registers that native. **Client: No** means client source cannot call it.

Client StringSub uses a different, copied string-handle declaration. The searchable HTML edition shows authority and client declarations separately. Regenerate this section and HTML with `node tools/generate-scripting-reference.cjs`; `--check` checks for drift. The generator reads routing from implementations and fails for missing/unclassified registrations.

### Session, maps & persistence

| Declaration | Client | Implemented handling |
|---|---|---|
| `void StartCredits(string &in asMusic, bool abLoopMusic, string &in asTextCat, string &in asTextEntry, int alEndNum)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AddKeyPart(int alKeyPart)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void StartDemoEnd()` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AutoSave()` | No | The native offline save handler suppresses autosaves in multiplayer/session worlds. No multiplayer disk-save format. |
| `void CheckPoint(string &in asName,string &in asStartPos ,string &in asCallback, string &in asDeathHintCat, string &in asDeathHintEntry)` | No | Retain the shared native checkpoint policy. Multiplayer respawn does not replay checkpoint callbacks or reset other players/enemies. |
| `void ChangeMap(string &in asMapName, string &in asStartPos, string &in asStartSound, string &in asEndSound)` | No | Use the coordinated authority map transition. Existing settings gate remote level-door requests and attributed remote callbacks. |
| `void ClearSavedMaps()` | No | Clear the authority saved-map collection. Does not clear the downloaded map resource cache. |

### World entities & lifecycle

| Declaration | Client | Implemented handling |
|---|---|---|
| `void SetEntityActive(string &in asName, bool abActive)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetEntityVisible(string &in asName, bool abVisible)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `bool GetEntityExists(string &in asName)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void SetEntityPos(string &in asName, float afX, float afY, float afZ)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `float GetEntityPosX(string &in asName)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float GetEntityPosY(string &in asName)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float GetEntityPosZ(string &in asName)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void SetEntityCustomFocusCrossHair(string &in asName, string &in asCrossHair)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void CreateEntityAtArea(string &in asEntityName, string &in asEntityFile, string &in asAreaName, bool abFullGameSave)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void ReplaceEntity(string &in asName, string &in asBodyName, string &in asNewEntityName, string &in asNewEntityFile, bool abFullGameSave)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void PlaceEntityAtEntity(string &in asName, string &in asTargetEntity, string &in asTargetBodyName, bool abUseRotation)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetEntityInteractionDisabled(string& asName, bool abDisabled)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetPropActiveAndFade(string &in asName, bool abActive, float afFadeTime)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetPropHealth(string &in asName, float afHealth)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AddPropHealth(string &in asName, float afHealth)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `float GetPropHealth(string &in asName)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void ResetProp(string &in asName)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void PlayPropAnimation(string &in asProp, string &in asAnimation, float afFadeTime, bool abLoop, string &in asCallback)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |

### Physics, movement & attachments

| Declaration | Client | Implemented handling |
|---|---|---|
| `bool GetEntitiesCollide(string &in asEntityA, string &in asEntityB)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void SetPropStaticPhysics(string &in asName, bool abX)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `bool GetPropIsInteractedWith(string &in asName)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void RotatePropToSpeed(string &in asName, float afAcc, float afGoalSpeed, float afAxisX, float afAxisY, float afAxisZ, bool abResetSpeed, string &in asOffsetArea)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void StopPropMovement(string &in asName)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AddAttachedPropToProp(string& asPropName, string& asAttachName, string& asAttachFile, float fPosX, float fPosY, float fPosZ, float fRotX, float fRotY, float fRot)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AttachPropToProp(string& asPropName, string& asAttachName, string& asAttachFile, float fPosX, float fPosY, float fPosZ, float fRotX, float fRotY, float fRot)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void RemoveAttachedPropFromProp(string& asPropName, string& asAttachName)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetAllowStickyAreaAttachment(bool abX)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AttachPropToStickyArea(string &in asAreaName, string &in asProp)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AttachBodyToStickyArea(string& asAreaName, string& asBody)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void DetachFromStickyArea(string &in asAreaName)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AddPropForce(string &in asName, float afX, float afY, float afZ, string &in asCoordSystem)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AddPropImpulse(string &in asName, float afX, float afY, float afZ, string &in asCoordSystem)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AddBodyForce(string &in asName, float afX, float afY, float afZ, string &in asCoordSystem)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AddBodyImpulse(string &in asName, float afX, float afY, float afZ, string &in asCoordSystem)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void BreakJoint(string &in asName)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetBodyMass(string &in asName, float afMass)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `float GetBodyMass(string &in asName)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |

### Doors, puzzles & connections

| Declaration | Client | Implemented handling |
|---|---|---|
| `void SetSwingDoorLocked(string &in asName, bool abLocked, bool abEffects)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetSwingDoorClosed(string &in asName, bool abClosed, bool abEffects)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetSwingDoorDisableAutoClose(string &in asName, bool abDisableAutoClose)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetLevelDoorLocked(string &in asName, bool abLocked)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetLevelDoorLockedSound(string &in asName, string &in asSound)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetLevelDoorLockedText(string &in asName, string &in asTextCat, string &in asTextEntry)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `bool GetSwingDoorLocked(string &in asName)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `bool GetSwingDoorClosed(string &in asName)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `int GetSwingDoorState(string &in asName)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void SetPropObjectStuckState(string &in asName, int alState)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetWheelAngle(string &in asName, float afAngle, bool abAutoMove)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetWheelStuckState(string &in asName, int alState, bool abEffects)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetLeverStuckState(string &in asName, int alState, bool abEffects)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetWheelInteractionDisablesStuck(string &in asName, bool abX)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetLeverInteractionDisablesStuck(string &in asName, bool abX)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `int GetLeverState(string &in asName)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void SetMultiSliderStuckState(string &in asName, int alStuckState, bool abEffects)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetButtonSwitchedOn(string &in asName, bool abSwitchedOn, bool abEffects)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetMoveObjectState(string &in asName, float afState)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetMoveObjectStateExt(string &in asName, float afState, float afAcc, float afMaxSpeed, float afSlowdownDist, bool abResetSpeed)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void InteractConnectPropWithRope(string &in asName, string& asLeverName, string& asPropName, bool abInteractOnly, float afSpeedMul,float afMinSpeed, float afMaxSpeed, bool abInvert, int alStatesUsed)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void InteractConnectPropWithMoveObject(string &in asName, string &in asPropName, string &in asMoveObjectName, bool abInteractOnly,bool abInvert, int alStatesUsed)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void ConnectEntities(string &in asName, string &in asMainEntity, string &in asConnectEntity, bool abInvertStateSent, int alStatesUsed, string &in asCallbackFunc)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |

### Lighting & prop effects

| Declaration | Client | Implemented handling |
|---|---|---|
| `void SetLightVisible(string &in asLightName, bool abVisible)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void FadeLightTo(string &in asLightName, float afR, float afG, float afB, float afA, float afRadius, float afTime)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetLightFlickerActive(string& asLightName, bool abActive)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetPropEffectActive(string &in asName, bool abActive, bool abFadeAndPlaySounds)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetLampLit(string &in asName, bool abLit, bool abEffects)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |

### Enemies & AI

| Declaration | Client | Implemented handling |
|---|---|---|
| `void SetEnemyDisabled(string &in asName, bool abDisabled)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetEnemyIsHallucination(string &in asName, bool abX)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void FadeEnemyToSmoke(string &in asName, bool abPlaySound)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetEnemyDisableTriggers(string &in asName, bool abX)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void ShowEnemyPlayerPosition(string &in asName)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AlertEnemyOfPlayerPresence(string &in asName)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AddEnemyPatrolNode(string &in asEnemyName, string &in asNodeName, float afWaitTime, string &in asAnimation)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void ClearEnemyPatrolNodes(string &in asEnemyName)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetEnemySanityDecreaseActive(string &in asName, bool abX)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void TeleportEnemyToNode(string &in asEnemyName, string &in asNodeName, bool abChangeY)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void TeleportEnemyToEntity(string &in asEnemyName, string &in asTargetEntity, string &in asTargetBody, bool abChangeY)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void ChangeManPigPose(string&in asName, string&in asPoseType)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetTeslaPigFadeDisabled(string&in asName, bool abX)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetTeslaPigSoundDisabled(string&in asName, bool abX)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetTeslaPigEasyEscapeDisabled(string&in asName, bool abX)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void ForceTeslaPigSighting(string&in asName)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `string& GetEnemyStateName(string &in asName)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |

### NPC presentation

| Declaration | Client | Implemented handling |
|---|---|---|
| `void SetNPCAwake(string &in asName, bool abAwake, bool abEffects)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetNPCFollowPlayer(string &in asName, bool abX)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |

### Player body, movement & control

| Declaration | Client | Implemented handling |
|---|---|---|
| `void SetPlayerActive(bool abActive)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void ChangePlayerStateToNormal()` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetPlayerCrouching(bool abCrouch)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void AddPlayerBodyForce(float afX, float afY, float afZ, bool abUseLocalCoords)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetPlayerPos(float afX, float afY, float afZ)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `float GetPlayerPosX()` | Yes | Read the selected actor on authority, or the owning local actor on a client. Replicated remote data can lag; missing actors do not fall back to the host. |
| `float GetPlayerPosY()` | Yes | Read the selected actor on authority, or the owning local actor on a client. Replicated remote data can lag; missing actors do not fall back to the host. |
| `float GetPlayerPosZ()` | Yes | Read the selected actor on authority, or the owning local actor on a client. Replicated remote data can lag; missing actors do not fall back to the host. |
| `float GetPlayerSpeed()` | Yes | Read the selected actor on authority, or the owning local actor on a client. Replicated remote data can lag; missing actors do not fall back to the host. |
| `float GetPlayerYSpeed()` | Yes | Read the selected actor on authority, or the owning local actor on a client. Replicated remote data can lag; missing actors do not fall back to the host. |
| `void MovePlayerForward(float afAmount)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetPlayerMoveSpeedMul(float afMul)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetPlayerRunSpeedMul(float afMul)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetPlayerJumpForceMul(float afMul)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetPlayerJumpDisabled(bool abX)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetPlayerCrouchDisabled(bool abX)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void TeleportPlayer(string &in asStartPosName)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |

### Health, sanity & lantern

| Declaration | Client | Implemented handling |
|---|---|---|
| `void SetInDarknessEffectsActive(bool abX)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetPlayerSanity(float afSanity)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void AddPlayerSanity(float afSanity)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `float GetPlayerSanity()` | Yes | Read the selected actor on authority, or the owning local actor on a client. Replicated remote data can lag; missing actors do not fall back to the host. |
| `void SetPlayerHealth(float afHealth)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void AddPlayerHealth(float afHealth)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `float GetPlayerHealth()` | Yes | Read the selected actor on authority, or the owning local actor on a client. Replicated remote data can lag; missing actors do not fall back to the host. |
| `void SetPlayerLampOil(float afOil)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void AddPlayerLampOil(float afOil)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `float GetPlayerLampOil()` | Yes | Read the selected actor on authority, or the owning local actor on a client. Replicated remote data can lag; missing actors do not fall back to the host. |
| `void SetSanityDrainDisabled(bool abX)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void GiveSanityBoost()` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void GiveSanityBoostSmall()` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void GiveSanityDamage(float afAmount, bool abUseEffect)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void GivePlayerDamage(float afAmount, string &in asType, bool abSpinHead, bool abLethal)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetPlayerFallDamageDisabled(bool abX)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetLanternActive(bool abX, bool abUseEffects)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `bool GetLanternActive()` | Yes | Read the selected actor on authority, or the owning local actor on a client. Replicated remote data can lag; missing actors do not fall back to the host. |
| `void SetLanternDisabled(bool abX)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |

### Inventory & item use

| Declaration | Client | Implemented handling |
|---|---|---|
| `void ExitInventory()` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetInventoryDisabled(bool abX)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetInventoryMessage(string &in asTextCategory, string &in asTextEntry, float afTime)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void GiveItem(string &in asName, string &in asType, string &in asSubTypeName, string &in asImageName, float afAmount)` | No | With an authority actor, grant to that actor. Without one, retain existing shared script-grant policy; combination routing takes precedence. |
| `void GiveItemFromFile(string& asName, string& asFileName)` | No | With an authority actor, grant to that actor. Without one, retain existing shared script-grant policy; combination routing takes precedence. |
| `void RemoveItem(string &in asName)` | No | With an authority actor, remove from that actor; without one, retain shared removal. Logical shared grants retain their original shared-removal policy. Combination routing takes precedence. |
| `bool HasItem(string &in asName)` | No | Require an authority actor in Version 2. Remote results use the supported progression/hand-object ledger, not a complete consumable mirror. |

### Quests, journals & progress

| Declaration | Client | Implemented handling |
|---|---|---|
| `void AddNote(string &in asNameAndTextEntry, string &in asImage)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AddDiary(string &in asNameAndTextEntry, string &in asImage)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void AddQuest(string &in asName, string &in asNameAndTextEntry)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void CompleteQuest(string &in asName, string &in asNameAndTextEntry)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `bool QuestIsCompleted(string &in asName)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `bool QuestIsAdded(string &in asName)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void SetNumberOfQuestsInMap(int alNumberOfQuests)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |

### Camera & screen effects

| Declaration | Client | Implemented handling |
|---|---|---|
| `void FadeIn(float afTime)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void FadeOut(float afTime)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void FadeImageTrailTo(float afAmount, float afSpeed)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void FadeSepiaColorTo(float afAmount, float afSpeed)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void FadeRadialBlurTo(float afSize, float afSpeed)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void SetRadialBlurStartDist(float afStartDist)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void StartEffectFlash(float afFadeIn, float afWhite, float afFadeOut)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void StartScreenShake(float afAmount, float afTime, float afFadeInTime,float afFadeOutTime)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void FadePlayerFOVMulTo(float afX, float afSpeed)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void FadePlayerAspectMulTo(float afX, float afSpeed)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void FadePlayerRollTo(float afX, float afSpeedMul, float afMaxSpeed)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void MovePlayerHeadPos(float afX, float afY, float afZ, float afSpeed, float afSlowDownDist)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void StartPlayerLookAt(string &in asEntityName, float afSpeedMul, float afMaxSpeed,string &in asAtTargetCallback)` | No | Authority actor required. Local controller execution with a one-shot host-issued completion token when a callback is supplied. |
| `void StopPlayerLookAt()` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void SetPlayerLookSpeedMul(float afMul)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |

### Messages, hints & interface

| Declaration | Client | Implemented handling |
|---|---|---|
| `void SetupLoadScreen(string &in asTextCat, string &in asTextEntry, int alRandomNum, string &in asImageFile)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void ShowPlayerCrossHairIcons(bool abX)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void SetMessage(string &in asTextCategory, string &in asTextEntry, float afTime)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void SetDeathHint(string &in asTextCategory, string &in asTextEntry)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void ReturnOpenJournal(bool abOpenJournal)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void GiveHint(string &in asName, string &in asMessageCat, string &in asMessageEntry, float afTimeShown)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void RemoveHint(string &in asName)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void BlockHint(string &in asName)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void UnBlockHint(string &in asName)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |

### Insanity & flashback effects

| Declaration | Client | Implemented handling |
|---|---|---|
| `void StartEffectEmotionFlash(string &in asTextCat, string &in asTextEntry, string &in asSound)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `bool GetFlashbackIsActive()` | Yes | Selected actor required. Authority can query its local presentation; remote presentation queries fail explicitly. Client modules read their own subsystem. |
| `void SetInsanitySetEnabled(string &in asSet, bool abX)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void StartRandomInsanityEvent()` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void StartInsanityEvent(string &in asEventName)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void StopCurrentInsanityEvent()` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `bool InsanityEventIsActive()` | Yes | Selected actor required. Authority can query its local presentation; remote presentation queries fail explicitly. Client modules read their own subsystem. |

### Sky, fog & map presentation

| Declaration | Client | Implemented handling |
|---|---|---|
| `void SetMapDisplayNameEntry(string &in asNameEntry)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetSkyBoxActive(bool abActive)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetSkyBoxTexture(string &in asTexture)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetSkyBoxColor(float afR, float afG, float afB, float afA)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetFogActive(bool abActive)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetFogColor(float afR, float afG, float afB, float afA)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetFogProperties(float afStart, float afEnd, float afFalloffExp, bool abCulling)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |

### Particles & player-centered weather

| Declaration | Client | Implemented handling |
|---|---|---|
| `void StartPlayerSpawnPS(string &in asSPSFile)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void StopPlayerSpawnPS()` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void CreateParticleSystemAtEntity(string &in asPSName, string &in asPSFile, string &in asEntity, bool abSavePS)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void CreateParticleSystemAtEntityExt(	string &in asPSName, string &in asPSFile, string &in asEntity, bool abSavePS, float afR, float afG, float afB, float afA, bool abFadeAtDistance, float afFadeMinEnd, float afFadeMinStart, float afFadeMaxStart, float afFadeMaxEnd)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void DestroyParticleSystem(string &in asName)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |

### Sound, voices & music

| Declaration | Client | Implemented handling |
|---|---|---|
| `void AddEffectVoice(string &in asVoiceFile, string &in asEffectFile, string &in asTextCat, string &in asTextEntry, bool abUsePostion,  string &in asPosEnitity, float afMinDistance, float afMaxDistance)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void StopAllEffectVoices(float afFadeOutTime)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `bool GetEffectVoiceActive()` | Yes | Selected actor required. Authority can query its local presentation; remote presentation queries fail explicitly. Client modules read their own subsystem. |
| `void PlayGuiSound(string &in asSoundFile, float afVolume)` | Yes | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. Client calls affect their own local subsystem without explicit selection. |
| `void SetPlayerPermaDeathSound(string &in asSound)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void DisableDeathStartSound()` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void PlaySoundAtEntity(string &in asSoundName, string &in asSoundFile, string &in asEntity, float afFadeSpeed, bool abSaveSound)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void FadeInSound(string& asSoundName, float afFadeTime, bool abPlayStart)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void StopSound(string &in asSoundName, float afFadeTime)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void PlayMusic(string &in asMusicFile, bool abLoop, float afVolume, float afFadeTime, int alPrio, bool abResume)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void StopMusic(float afFadeTime, int alPrio)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void FadeGlobalSoundVolume(float afDestVolume, float afTime)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |
| `void FadeGlobalSoundSpeed(float afDestSpeed, float afTime)` | No | Authority requires a selected actor and executes through that actor’s controller/presentation subsystem; remote delivery is directed and excluded from shared replay history. |

### Account & achievements

| Declaration | Client | Implemented handling |
|---|---|---|
| `void UnlockAchievement(string &in asName)` | No | Target the selected account, or the host and initialized participants without an actor. Transient, excluded from initial/history replay. |

### Timers & deferred execution

| Declaration | Client | Implemented handling |
|---|---|---|
| `void AddTimer(string &in asName, float afTime, string &in asFunction)` | Yes | Authority: capture module and optional actor/session/character life. Client: private module timer with typed string callback. |
| `void RemoveTimer(string &in asName)` | Yes | Remove timers owned by the current authority context or current client module. |
| `float GetTimerTimeLeft(string &in asName)` | Yes | Read the timer owned by the current authority context or current client module; absent timers return zero. |

### Callback bindings

| Declaration | Client | Implemented handling |
|---|---|---|
| `void SetEffectVoiceOverCallback(string &in asFunc)` | No | Authority actor required. Bind a one-shot host-issued voice-completion token to actor, module and character life. |
| `void SetLanternLitCallback(string &in asCallback)` | No | Authority actor required. Bind a persistent host-issued lantern token; empty callback clears the binding. |
| `void AddCombineCallback(string &in asName, string &in asItemA, string &in asItemB, string &in asFunction, bool abAutoDestroy)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void RemoveCombineCallback(string &in asName)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void AddUseItemCallback(string &in asName, string &in asItem, string &in asEntity, string &in asFunction, bool abAutoDestroy)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void RemoveUseItemCallback(string &in asName)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void SetEntityPlayerLookAtCallback(string &in asName, string &in asCallback, bool abRemoveWhenLookedAt)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void SetEntityPlayerInteractCallback(string &in asName, string &in asCallback, bool abRemoveOnInteraction)` | No | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void SetEntityCallbackFunc(string &in asName, string &in asCallback)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void SetEntityConnectionStateChangeCallback(string& asName, string& asCallback)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void SetMultiSliderCallback(string &in asName, string &in asCallback)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void AddEntityCollideCallback(string &in asParentName, string &in asChildName, string &in asFunction, bool abDeleteOnCollide, int alStates)` | No | Authority combined occupancy for parent Player: first entrance/final exit; one-shot removal applies to everyone. Other parents retain native policy. |
| `void RemoveEntityCollideCallback(string &in asParentName, string &in asChildName)` | No | Remove the native combined/entity registration. Does not remove the explicit per-player observer. |

### Map & cross-map variables

| Declaration | Client | Implemented handling |
|---|---|---|
| `void SetLocalVarInt(string &in asName, int alVal)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `void SetLocalVarFloat(string &in asName, float afVal)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `void SetLocalVarString(string &in asName, string &in asVal)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `void AddLocalVarInt(string &in asName, int alVal)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `void AddLocalVarFloat(string &in asName, float afVal)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `void AddLocalVarString(string &in asName, string &in asVal)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `int GetLocalVarInt(string &in asName)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `float GetLocalVarFloat(string &in asName)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `string& GetLocalVarString(string &in asName)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `void SetGlobalVarInt(string &in asName, int alVal)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `void SetGlobalVarFloat(string &in asName, float afVal)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `void SetGlobalVarString(string &in asName, string &in asVal)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `void AddGlobalVarInt(string &in asName, int alVal)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `void AddGlobalVarFloat(string &in asName, float afVal)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `void AddGlobalVarString(string &in asName, string &in asVal)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `int GetGlobalVarInt(string &in asName)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `float GetGlobalVarFloat(string &in asName)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |
| `string& GetGlobalVarString(string &in asName)` | No | Authority story store. Local means map lifetime; Global means cross-map lifetime. Legacy string& getters expose mutable authority storage. |

### Preloading & resource caches

| Declaration | Client | Implemented handling |
|---|---|---|
| `void CreateDataCache()` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void DestroyDataCache()` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void PreloadParticleSystem(string& asPSFile)` | Yes | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |
| `void PreloadSound(string& asSoundFile)` | Yes | Authority native with its existing shared native-effect synchronization. Selecting an actor does not redirect this operation. |

### Math, strings, randomness & diagnostics

| Declaration | Client | Implemented handling |
|---|---|---|
| `void Print(string &in asString)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void AddDebugMessage(string &in asString, bool abCheckForDuplicates)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `void ProgLog(string &in asLevel, string &in asMessage)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `bool ScriptDebugOn()` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float RandFloat(float afMin, float afMax)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `int RandInt(int alMin, int alMax)` | No | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `bool StringContains(string &in asString, string &in asSubString)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `string& StringSub(string &in asString, int alStart, int alCount)` | Yes | Return a bounded substring. Client declaration returns a fresh string@; authority keeps its legacy string& declaration. See string ownership below. |
| `float MathSin(float afX)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float MathCos(float afX)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float MathTan(float afX)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float MathAsin(float afX)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float MathAcos(float afX)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float MathAtan(float afX)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float MathAtan2(float afX, float afY)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float MathSqrt(float afX)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float MathPow(float afBase, float afExp)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float MathMin(float afA, float afB)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float MathMax(float afA, float afB)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float MathClamp(float afX, float afMin, float afMax)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float MathAbs(float afX)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `int StringToInt(string&in asString)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `float StringToFloat(string&in asString)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |
| `bool StringToBool(string&in asString)` | Yes | Authority native with its existing engine/multiplayer handling. Selecting an actor does not add personal routing. Client availability is listed explicitly. |

### Version 2 helpers

| Declaration | Client | Implemented handling |
|---|---|---|
| `bool SelectScriptPlayer(int player)` | No | Select an available actor for the current authority dispatch. Invalid actors raise a script error; nested calls restore the caller. |
| `int GetScriptPlayerId()` | Yes | Return the current actor, or -1 in an unattributed authority context. Client modules return their owning local player. |
| `int GetScriptPlayerCount()` | No | Authority-only enumeration of the hosting player and available remote actors. |
| `int GetScriptPlayerIdAt(int index)` | No | Authority-only enumeration, local actor first and remote IDs in order. An invalid index returns -1. |
| `void RunClientCallback(string &in function,string &in argument)` | No | Authority-only typed string callback to the selected actor, in the matching initialized client module. |
| `void AddPlayerCollideCallback(string &in child,string &in function,bool oncePerPlayer,int states)` | No | Authority registration of an attributed per-player observer. Optional once-per-participant delivery; separate from combined occupancy. |
| `void RemovePlayerCollideCallback(string &in child)` | No | Remove the explicit per-player observer for this child. Does not remove the combined observer. |
| `void SetPlayerVarInt(string &in name,int value)` | No | Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps. |
| `void SetPlayerVarInt(string &in name,int value,bool campaign)` | No | Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps. |
| `int GetPlayerVarInt(string &in name)` | No | Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps. |
| `int GetPlayerVarInt(string &in name,bool campaign)` | No | Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps. |
| `void SetPlayerVarFloat(string &in name,float value)` | No | Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps. |
| `void SetPlayerVarFloat(string &in name,float value,bool campaign)` | No | Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps. |
| `float GetPlayerVarFloat(string &in name)` | No | Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps. |
| `float GetPlayerVarFloat(string &in name,bool campaign)` | No | Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps. |
| `void SetPlayerVarString(string &in name,string &in value)` | No | Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps. |
| `void SetPlayerVarString(string &in name,string &in value,bool campaign)` | No | Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps. |
| `string@ GetPlayerVarString(string &in name)` | No | Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps. |
| `string@ GetPlayerVarString(string &in name,bool campaign)` | No | Authority-only selected-actor store, isolated by module. Short overload: map lifetime; campaign=true: current participant session across maps. |
| `void PublishPlayerVar(string &in name,string &in value)` | No | Authority-only publication of a copied string view to the selected actor. The private store is not exposed. |
| `string@ GetPublishedScriptVar(string &in name)` | Yes | Return a fresh string handle from the current actor/module published view. No write-through access. |

<!-- END GENERATED NATIVE API -->
