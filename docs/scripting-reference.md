# Amnesia64 scripting reference

> **Design draft · 16 September 2026**  
> Complete native API inventory: **260 functions**. Scope and delivery describe the proposed multiplayer design; declarations reflect the current engine registrations.

Based on the supplied `scripts.txt` and the current Amnesia64 implementation. The original file is unchanged.

[Open the searchable HTML edition](./scripting-reference.html).

## How to read this reference

- The scope, impact, delivery and handling notes below are a design classification, not a claim that the proposed routing already exists.
- Map and global scripts currently execute on the host; selected native effects are replicated. Inventory scripting has a separate path and is not uniformly guarded against client execution.
- Many player operations still access gpBase->mpPlayer or process-local managers. Generalized remote-player context and per-domain storage require implementation work.
- All 260 active native registrations in LuxScriptHandler::InitScriptFunctions are included once in the catalog. Historical commented registrations and the five engine lifecycle entry points are outside that count.
- Declarations preserve the currently registered API, with whitespace normalized. Known binding defects are marked; the source code has not been corrected by this document.

### Scope key

| Scope | Meaning |
|---|---|
| **Shared world** | One authoritative world or session value; replicate consequences as appropriate. This does not require executing the script on every machine. |
| **Selected player** | State or behavior belonging to a particular player. The host establishes the subject and directs the relevant local execution. |
| **Presentation** | Rendering, camera, interface or playback for an intended audience. Delivery may target one player or everyone. |
| **Script context** | Resolve storage, event, timer, query or callback behavior using its execution domain and player context. |
| **Local runtime** | Process-local utilities, bookkeeping or resources with no inherent broadcast requirement. |
| **Policy required** | Mixed ownership or an unresolved design choice. Specify shared versus personal behavior before implementing routing. |

**Impact** is separate from scope: **Gameplay**, **Presentation**, **Mixed**, **Read**, **Binding**, or **Utility**. A presentation operation can be delivered to everyone; a gameplay operation can target one player. These labels replace the ambiguous Critical/Moderate priority split.

## Execution contract

- **Authority and audience are separate.** A host-authorized operation may change the shared world, one player, or one player’s presentation. Global does not mean apply to every character; local execution does not mean a client independently decides gameplay.
- **Player context follows the event.** For an event attributed to Player B, host-side player queries and decisions refer to B’s host representation, and directed local work refers to B’s client character. The hosting player uses the same rule.
- **An operation keeps its own ownership.** A callback for B may unlock a shared door, damage B, and show B a message. These remain a world change, a player change, and a presentation effect respectively.
- **Preserve legacy behavior.** Existing unprefixed callbacks remain host-controlled with their existing supported consequences. Marking an operation as presentation-capable must not silently remove legacy delivery to remote players.
- **Reads have a subject and a time.** Player getters require the intended player. Client world queries observe local replicas, which may lag; authoritative decisions use the host’s state. A read does not itself need broadcast execution.
- **Delayed work retains context.** Timers and callbacks need their execution domain, module, map generation, and player association. Separate runtime stores on the listen host prevent client work from overwriting server variables or registrations.

## Contents

- [Session, maps & persistence](#session) — 7 functions
- [World entities & lifecycle](#world_entities) — 18 functions
- [Physics, movement & attachments](#world_physics) — 19 functions
- [Doors, puzzles & connections](#world_mechanisms) — 23 functions
- [Lighting & prop effects](#lighting) — 5 functions
- [Enemies & AI](#enemies) — 17 functions
- [NPC presentation](#npc) — 2 functions
- [Player body, movement & control](#player_body) — 17 functions
- [Health, sanity & lantern](#player_vitals) — 19 functions
- [Inventory & item use](#inventory) — 7 functions
- [Quests, journals & progress](#progression) — 7 functions
- [Camera & screen effects](#camera) — 15 functions
- [Messages, hints & interface](#interface) — 9 functions
- [Insanity & flashback effects](#player_effects) — 7 functions
- [Sky, fog & map presentation](#environment) — 7 functions
- [Particles & player-centered weather](#particles) — 5 functions
- [Sound, voices & music](#audio) — 13 functions
- [Account & achievements](#account) — 1 function
- [Timers & deferred execution](#timers) — 3 functions
- [Callback bindings](#callbacks) — 13 functions
- [Map & cross-map variables](#variables) — 18 functions
- [Preloading & resource caches](#resources) — 4 functions
- [Math, strings, randomness & diagnostics](#utility) — 24 functions
- [Engine lifecycle hooks](#engine-lifecycle-hooks)
- [Open design decisions](#open-design-decisions)
- [Known binding defects](#known-binding-defects)
- [Source notes](#source-notes)

<a id="session"></a>

## Session, maps & persistence

Session transitions and persistent world bookkeeping. Coordinate session-wide decisions; separate their local screens and audio.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `StartCredits` | Policy required | Mixed | Authorize a session ending on the host, then present it to the selected audience. Changes the local updater container to Credits; independent player endings require an explicit session policy. |
| `AddKeyPart` | Local runtime | Utility | Update local credits bookkeeping; distribute the value only if the ending requires it. Appends an integer to the credits key buffer; it does not grant a world item. |
| `StartDemoEnd` | Policy required | Mixed | Authorize a session ending on the host, then present it to the selected audience. Switches the local updater container when a demo-end handler exists. |
| `AutoSave` | Policy required | Gameplay | Keep disabled during multiplayer until a session-save policy is implemented. The current save handler suppresses offline autosaves for multiplayer/session worlds. |
| `CheckPoint` | Policy required | Mixed | Host-authorize shared checkpoints; use an explicit player target for personal respawn points. Combines spawn position, callback, music snapshot and death hint. Multiplayer death currently respawns only that player without resetting enemies or running the checkpoint script. |
| `ChangeMap` | Shared world | Gameplay | Commit on the host through the coordinated map-change path; clients may submit permitted requests. The session must agree on one map and generation; independently changing a client's map is not a local effect. |
| `ClearSavedMaps` | Shared world | Gameplay | Clear the authoritative saved-map collection on the host. Resets persisted map progression; distinct from local resource-cache maintenance. |

### Registered declarations

```cpp
void StartCredits(
    string &in asMusic,
    bool abLoopMusic,
    string &in asTextCat,
    string &in asTextEntry,
    int alEndNum
);
void AddKeyPart(int alKeyPart);
void StartDemoEnd();
void AutoSave();
void CheckPoint(
    string &in asName,
    string &in asStartPos,
    string &in asCallback,
    string &in asDeathHintCat,
    string &in asDeathHintEntry
);
void ChangeMap(
    string &in asMapName,
    string &in asStartPos,
    string &in asStartSound,
    string &in asEndSound
);
void ClearSavedMaps();
```

Registration references: [StartCredits](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:414) · [AddKeyPart](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:415) · [StartDemoEnd](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:417) · [AutoSave](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:419) · [CheckPoint](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:420) · [ChangeMap](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:422) · [ClearSavedMaps](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:423).

<a id="world_entities"></a>

## World entities & lifecycle

One canonical state for each shared entity. Presentation-only visibility and animation require explicit handling.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `SetEntityActive` | Shared world | Gameplay | Authority changes entity activity and replicates the result. Activates or deactivates the actual entity, potentially changing bodies, AI, triggers and interaction; use visibility for mesh-only hiding. |
| `SetEntityVisible` | Presentation | Presentation | Apply a visibility override to selected players. Changes mesh visibility only; collision, activity, enemy behavior and sanity effects remain separately controlled. |
| `GetEntityExists` | Shared world | Read | Query authority for gameplay; local replicas for presentation. Reports whether the named entity exists in the current map; a client result reflects its available replicated or private entities. |
| `SetEntityPos` | Shared world | Gameplay | Authority moves the entity and replicates its transform. Moves enemy feet or the first physics body; independent client moves would disagree about the same object's position. |
| `GetEntityPosX` | Shared world | Read | Read the authoritative or local replica position as appropriate. Returns enemy feet X or the entity body's X; client presentation may observe an interpolated or delayed replica. |
| `GetEntityPosY` | Shared world | Read | Read the authoritative or local replica position as appropriate. Returns enemy feet Y or the entity body's Y; client presentation may observe an interpolated or delayed replica. |
| `GetEntityPosZ` | Shared world | Read | Read the authoritative or local replica position as appropriate. Returns enemy feet Z or the entity body's Z; client presentation may observe an interpolated or delayed replica. |
| `SetEntityCustomFocusCrossHair` | Presentation | Presentation | Override the focus icon for selected players. Changes presentation metadata, not whether interaction is authorized or physically possible. |
| `CreateEntityAtArea` | Shared world | Gameplay | Authority creates a shared entity and replicates its identity and state. Loads a real entity at the area's transform, including its bodies and behaviors. Private decorations require an explicitly cosmetic creation path. |
| `ReplaceEntity` | Shared world | Gameplay | Authority replaces the shared entity and replicates the replacement. Destroys the old entity and loads a new one at its body's transform; this is more than a mesh swap. |
| `PlaceEntityAtEntity` | Shared world | Gameplay | Authority places the source body and replicates the transform. Copies a target body's position and optionally rotation; changes actual physical placement. |
| `SetEntityInteractionDisabled` | Shared world | Gameplay | Authority enforces shared or explicit per-player interaction permission. Existing semantics store one prop-wide flag. Per-player restrictions require a permission layer, not merely hiding a local prompt. |
| `SetPropActiveAndFade` | Shared world | Mixed | Authority changes activity; peers present the associated fade. Deactivates or activates the real prop and adds a visual fade or dissolve; it is not a visibility-only effect. |
| `SetPropHealth` | Shared world | Gameplay | Authority sets shared prop health and replicates resulting changes. Health changes call the prop's native health-change behavior and may cause breakage or other state changes. |
| `AddPropHealth` | Shared world | Gameplay | Authority adjusts shared prop health and replicates resulting changes. Applies a health delta to the same destructible object; execute once to avoid duplicate damage or healing. |
| `GetPropHealth` | Shared world | Read | Query authority for decisions; use replicated health for local display. Reads the prop's current health without applying damage or triggering health-change callbacks. |
| `ResetProp` | Shared world | Gameplay | Authority resets the prop and replicates its resulting state. Runs the prop's property-reset behavior; this is broader than resetting only its visual appearance. |
| `PlayPropAnimation` | Shared world | Mixed | Share physical or gameplay animation; isolate cosmetic playback. May move animated bodies or produce events, and stores an end callback. Private playback needs an explicit presentation-only contract. |

### Registered declarations

```cpp
void SetEntityActive(string &in asName, bool abActive);
void SetEntityVisible(string &in asName, bool abVisible);
bool GetEntityExists(string &in asName);
void SetEntityPos(string &in asName, float afX, float afY, float afZ);
float GetEntityPosX(string &in asName);
float GetEntityPosY(string &in asName);
float GetEntityPosZ(string &in asName);
void SetEntityCustomFocusCrossHair(string &in asName, string &in asCrossHair);
void CreateEntityAtArea(
    string &in asEntityName,
    string &in asEntityFile,
    string &in asAreaName,
    bool abFullGameSave
);
void ReplaceEntity(
    string &in asName,
    string &in asBodyName,
    string &in asNewEntityName,
    string &in asNewEntityFile,
    bool abFullGameSave
);
void PlaceEntityAtEntity(
    string &in asName,
    string &in asTargetEntity,
    string &in asTargetBodyName,
    bool abUseRotation
);
void SetEntityInteractionDisabled(string& asName, bool abDisabled);
void SetPropActiveAndFade(string &in asName, bool abActive, float afFadeTime);
void SetPropHealth(string &in asName, float afHealth);
void AddPropHealth(string &in asName, float afHealth);
float GetPropHealth(string &in asName);
void ResetProp(string &in asName);
void PlayPropAnimation(
    string &in asProp,
    string &in asAnimation,
    float afFadeTime,
    bool abLoop,
    string &in asCallback
);
```

Registration references: [SetEntityActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:575) · [SetEntityVisible](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:576) · [GetEntityExists](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:577) · [SetEntityPos](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:578) · [GetEntityPosX](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:579) · [GetEntityPosY](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:580) · [GetEntityPosZ](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:581) · [SetEntityCustomFocusCrossHair](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:582) · [CreateEntityAtArea](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:583) · [ReplaceEntity](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:584) · [PlaceEntityAtEntity](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:585) · [SetEntityInteractionDisabled](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:590) · [SetPropActiveAndFade](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:594) · [SetPropHealth](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:652) · [AddPropHealth](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:653) · [GetPropHealth](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:654) · [ResetProp](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:655) · [PlayPropAnimation](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:656).

<a id="world_physics"></a>

## Physics, movement & attachments

Apply authoritative changes through the existing physics ownership and replication paths. Do not independently apply a gameplay impulse once per peer.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `GetEntitiesCollide` | Shared world | Read | Use authoritative collision for decisions; local collision for presentation. Tests entity collision in the current map. Replica overlap is not proof that authority accepted a gameplay event. |
| `SetPropStaticPhysics` | Shared world | Gameplay | Authority changes physical behavior and replicates it. Changes whether the shared prop uses static physics; clients must agree about movement and collision behavior. |
| `GetPropIsInteractedWith` | Shared world | Read | Resolve against authoritative interaction ownership for gameplay. For multiplayer, specify whether any player or a particular player owns the interaction; a local player-state check alone is insufficient. |
| `RotatePropToSpeed` | Shared world | Gameplay | Authority drives rotation and replicates resulting motion. Starts actual prop rotation, optionally around an area-derived offset, and marks the prop for full-game saving. |
| `StopPropMovement` | Shared world | Gameplay | Authority stops the movement controller and replicates the result. Stops scripted prop motion; it is not equivalent to only freezing the displayed mesh. |
| `AddAttachedPropToProp` | Shared world | Gameplay | Authority creates the attachment and replicates it. Deprecated alias of AttachPropToProp. The current forwarding uses the rotation-Z argument as position Z, so prefer the replacement API. |
| `AttachPropToProp` | Shared world | Gameplay | Authority creates the attached prop and replicates its relationship. Creates and attaches a real prop with a relative transform; attached body transforms follow the parent. |
| `RemoveAttachedPropFromProp` | Shared world | Gameplay | Authority destroys the attachment and replicates removal. Destroys the named attached prop, rather than only hiding its rendering. |
| `SetAllowStickyAreaAttachment` | Shared world | Gameplay | Authority sets the world's sticky-area attachment policy. This is a class-wide attachment gate, not a flag on one named area or one player. |
| `AttachPropToStickyArea` | Shared world | Gameplay | Authority attaches the prop body and replicates the relationship. Attaches the prop's main or first body to the area, changing actual physical behavior and potentially invoking attachment callbacks. |
| `AttachBodyToStickyArea` | Shared world | Gameplay | Authority attaches the named body and replicates the relationship. Creates a real sticky-area attachment; its callback and physics effects must have one event owner. |
| `DetachFromStickyArea` | Shared world | Gameplay | Authority detaches the shared body and replicates the result. Changes the area's actual attached-body state and may invoke its detach callback. |
| `AddPropForce` | Shared world | Gameplay | Authority applies the force and replicates resulting physics. Acts on prop bodies in the selected coordinate system; clients should not independently accumulate the same force. |
| `AddPropImpulse` | Shared world | Gameplay | Authority applies the impulse once and replicates resulting physics. Applies a discrete physical impulse to prop bodies; duplicate execution multiplies its effect. |
| `AddBodyForce` | Shared world | Gameplay | Authority applies the force to the shared body. Continuous local requests must become validated authoritative control or state updates, not independent replica forces. |
| `AddBodyImpulse` | Shared world | Gameplay | Authority applies the impulse once to the shared body. Use one authoritative application; replicate the resulting motion rather than replaying it repeatedly during late join. |
| `BreakJoint` | Shared world | Gameplay | Authority breaks the joint and replicates the structural change. Changes real physical connectivity and can cause body movement, destruction behavior or callbacks. |
| `SetBodyMass` | Shared world | Gameplay | Authority sets physical mass and replicates the change. Changes dynamics of the same shared body; peers must not independently choose different masses. |
| `GetBodyMass` | Shared world | Read | Read authoritative mass for gameplay; replica mass for display. Returns the body's mass without changing its physical state. |

### Registered declarations

```cpp
bool GetEntitiesCollide(string &in asEntityA, string &in asEntityB);
void SetPropStaticPhysics(string &in asName, bool abX);
bool GetPropIsInteractedWith(string &in asName);
void RotatePropToSpeed(
    string &in asName,
    float afAcc,
    float afGoalSpeed,
    float afAxisX,
    float afAxisY,
    float afAxisZ,
    bool abResetSpeed,
    string &in asOffsetArea
);
void StopPropMovement(string &in asName);
void AddAttachedPropToProp(
    string& asPropName,
    string& asAttachName,
    string& asAttachFile,
    float fPosX,
    float fPosY,
    float fPosZ,
    float fRotX,
    float fRotY,
    float fRot
);
void AttachPropToProp(
    string& asPropName,
    string& asAttachName,
    string& asAttachFile,
    float fPosX,
    float fPosY,
    float fPosZ,
    float fRotX,
    float fRotY,
    float fRot
);
void RemoveAttachedPropFromProp(string& asPropName, string& asAttachName);
void SetAllowStickyAreaAttachment(bool abX);
void AttachPropToStickyArea(string &in asAreaName, string &in asProp);
void AttachBodyToStickyArea(string& asAreaName, string& asBody);
void DetachFromStickyArea(string &in asAreaName);
void AddPropForce(string &in asName, float afX, float afY, float afZ, string &in asCoordSystem);
void AddPropImpulse(string &in asName, float afX, float afY, float afZ, string &in asCoordSystem);
void AddBodyForce(string &in asName, float afX, float afY, float afZ, string &in asCoordSystem);
void AddBodyImpulse(string &in asName, float afX, float afY, float afZ, string &in asCoordSystem);
void BreakJoint(string &in asName);
void SetBodyMass(string &in asName, float afMass);
float GetBodyMass(string &in asName);
```

Registration references: [GetEntitiesCollide](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:591) · [SetPropStaticPhysics](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:595) · [GetPropIsInteractedWith](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:596) · [RotatePropToSpeed](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:597) · [StopPropMovement](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:598) · [AddAttachedPropToProp](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:600) · [AttachPropToProp](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:601) · [RemoveAttachedPropFromProp](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:602) · [SetAllowStickyAreaAttachment](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:626) · [AttachPropToStickyArea](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:627) · [AttachBodyToStickyArea](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:628) · [DetachFromStickyArea](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:629) · [AddPropForce](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:662) · [AddPropImpulse](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:663) · [AddBodyForce](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:664) · [AddBodyImpulse](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:665) · [BreakJoint](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:666) · [SetBodyMass](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:667) · [GetBodyMass](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:668).

<a id="world_mechanisms"></a>

## Doors, puzzles & connections

Shared mechanisms retain one agreed state. Player-specific access rules, if added, need explicit permission data.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `SetSwingDoorLocked` | Shared world | Mixed | Authority sets the physical door lock and replicates its effects. A shared swing door needs one mechanical lock state; the effects option controls accompanying presentation. |
| `SetSwingDoorClosed` | Shared world | Mixed | Authority sets door closure and replicates physical state. Closure affects the same physical door for everyone; presentation may be targeted separately. |
| `SetSwingDoorDisableAutoClose` | Shared world | Gameplay | Authority sets the door's automatic-closing policy. Changes the shared mechanism's future behavior, not a viewer-specific preference. |
| `SetLevelDoorLocked` | Shared world | Gameplay | Authority enforces shared or explicit per-player travel permission. The existing door stores one lock flag. Personal permission is possible, but a shared map transition remains a session decision. |
| `SetLevelDoorLockedSound` | Presentation | Presentation | Configure feedback for selected players or the shared default. Selects the sound used when a locked level door is tried; it does not change access permission. |
| `SetLevelDoorLockedText` | Presentation | Presentation | Configure feedback for selected players or the shared default. Selects localized locked-door text; it does not change the door's lock state. |
| `GetSwingDoorLocked` | Shared world | Read | Query authority for gameplay; use replica state for local display. Reads the shared swing door's lock flag, not a general per-player access policy. |
| `GetSwingDoorClosed` | Shared world | Read | Query authority for gameplay; use replica state for local display. Reads the door's closed state; a client value may lag authority. |
| `GetSwingDoorState` | Shared world | Read | Query authority for gameplay; use replica state for local display. Reads the door's discrete state rather than issuing a movement or lock command. |
| `SetPropObjectStuckState` | Shared world | Gameplay | Authority changes the object's stuck state and replicates it. Changes physical object behavior; a local-only change would conflict with shared interaction. |
| `SetWheelAngle` | Shared world | Gameplay | Authority sets or drives the wheel angle and replicates it. Changes the mechanism's actual angle; automatic movement and connected mechanisms share the same authoritative result. |
| `SetWheelStuckState` | Shared world | Mixed | Authority sets wheel constraint state; peers present effects. Changes whether and where the shared wheel is stuck; effects do not make the constraint viewer-specific. |
| `SetLeverStuckState` | Shared world | Mixed | Authority sets lever constraint state; peers present effects. Changes the shared lever's physical state and associated interaction behavior. |
| `SetWheelInteractionDisablesStuck` | Shared world | Gameplay | Authority sets the shared wheel's interaction policy. Determines whether interaction releases the wheel's stuck state; the resulting physical state is common to all players. |
| `SetLeverInteractionDisablesStuck` | Shared world | Gameplay | Authority sets the shared lever's interaction policy. Determines whether interaction releases the lever's stuck state; the resulting physical state is common to all players. |
| `GetLeverState` | Shared world | Read | Query authority for gameplay; use replica state for local display. Reads the shared lever's discrete state without mutating it. |
| `SetMultiSliderStuckState` | Shared world | Mixed | Authority sets slider constraint state; peers present effects. Changes a shared slider's physical state, with optional accompanying effects. |
| `SetButtonSwitchedOn` | Shared world | Mixed | Authority sets the button state and propagates connections. Changes the shared button's logical state; optional effects can be routed separately. |
| `SetMoveObjectState` | Shared world | Gameplay | Authority drives the move object and replicates its motion. Changes the physical mechanism's target state; all players interact with the same resulting geometry. |
| `SetMoveObjectStateExt` | Shared world | Gameplay | Authority drives the move object with explicit motion parameters. Adds acceleration, maximum speed, slowdown distance and speed-reset controls to shared mechanism movement. |
| `InteractConnectPropWithRope` | Shared world | Gameplay | Authority owns the prop-to-rope mechanism connection. Creates a real interaction connection with speed, inversion and state filters; it is not only callback registration. |
| `InteractConnectPropWithMoveObject` | Shared world | Gameplay | Authority owns the prop-to-move-object connection. Connects interaction and state changes to a shared physical move object, with inversion and state filters. |
| `ConnectEntities` | Shared world | Gameplay | Authority propagates shared connection state; dispatch callback context separately. Creates a mechanism relationship with inversion, state filtering and an optional callback; private notification must not duplicate the state change. |

### Registered declarations

```cpp
void SetSwingDoorLocked(string &in asName, bool abLocked, bool abEffects);
void SetSwingDoorClosed(string &in asName, bool abClosed, bool abEffects);
void SetSwingDoorDisableAutoClose(string &in asName, bool abDisableAutoClose);
void SetLevelDoorLocked(string &in asName, bool abLocked);
void SetLevelDoorLockedSound(string &in asName, string &in asSound);
void SetLevelDoorLockedText(string &in asName, string &in asTextCat, string &in asTextEntry);
bool GetSwingDoorLocked(string &in asName);
bool GetSwingDoorClosed(string &in asName);
int GetSwingDoorState(string &in asName);
void SetPropObjectStuckState(string &in asName, int alState);
void SetWheelAngle(string &in asName, float afAngle, bool abAutoMove);
void SetWheelStuckState(string &in asName, int alState, bool abEffects);
void SetLeverStuckState(string &in asName, int alState, bool abEffects);
void SetWheelInteractionDisablesStuck(string &in asName, bool abX);
void SetLeverInteractionDisablesStuck(string &in asName, bool abX);
int GetLeverState(string &in asName);
void SetMultiSliderStuckState(string &in asName, int alStuckState, bool abEffects);
void SetButtonSwitchedOn(string &in asName, bool abSwitchedOn, bool abEffects);
void SetMoveObjectState(string &in asName, float afState);
void SetMoveObjectStateExt(
    string &in asName,
    float afState,
    float afAcc,
    float afMaxSpeed,
    float afSlowdownDist,
    bool abResetSpeed
);
void InteractConnectPropWithRope(
    string &in asName,
    string& asLeverName,
    string& asPropName,
    bool abInteractOnly,
    float afSpeedMul,
    float afMinSpeed,
    float afMaxSpeed,
    bool abInvert,
    int alStatesUsed
);
void InteractConnectPropWithMoveObject(
    string &in asName,
    string &in asPropName,
    string &in asMoveObjectName,
    bool abInteractOnly,
    bool abInvert,
    int alStatesUsed
);
void ConnectEntities(
    string &in asName,
    string &in asMainEntity,
    string &in asConnectEntity,
    bool abInvertStateSent,
    int alStatesUsed,
    string &in asCallbackFunc
);
```

Registration references: [SetSwingDoorLocked](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:605) · [SetSwingDoorClosed](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:606) · [SetSwingDoorDisableAutoClose](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:607) · [SetLevelDoorLocked](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:608) · [SetLevelDoorLockedSound](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:609) · [SetLevelDoorLockedText](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:610) · [GetSwingDoorLocked](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:611) · [GetSwingDoorClosed](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:612) · [GetSwingDoorState](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:613) · [SetPropObjectStuckState](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:614) · [SetWheelAngle](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:615) · [SetWheelStuckState](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:616) · [SetLeverStuckState](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:617) · [SetWheelInteractionDisablesStuck](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:618) · [SetLeverInteractionDisablesStuck](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:619) · [GetLeverState](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:620) · [SetMultiSliderStuckState](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:622) · [SetButtonSwitchedOn](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:625) · [SetMoveObjectState](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:658) · [SetMoveObjectStateExt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:659) · [InteractConnectPropWithRope](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:676) · [InteractConnectPropWithMoveObject](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:677) · [ConnectEntities](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:678).

<a id="lighting"></a>

## Lighting & prop effects

Existing light objects feed gameplay illumination as well as rendering. A personal visual override must not silently change gameplay lighting.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `SetLightVisible` | Shared world | Mixed | Replicate canonical lighting; isolate private render overrides. Light visibility also controls inclusion in gameplay light-level queries, affecting darkness and enemy detection. |
| `FadeLightTo` | Shared world | Mixed | Replicate gameplay lighting; use a separate private visual override. Changes color and radius, makes the light visible and disables flickering. These values also affect gameplay illumination. |
| `SetLightFlickerActive` | Shared world | Mixed | Replicate canonical flicker policy; isolate private visual flicker. Flicker changes the same light used for visibility and darkness calculations; it is not automatically cosmetic. |
| `SetPropEffectActive` | Shared world | Mixed | Replicate gameplay effects; separate private visual and audio overrides. Controls a bundle of particles, lights, sound and illumination. Lights and hearable sounds can affect gameplay. |
| `SetLampLit` | Shared world | Mixed | Authority sets lamp state; peers present light and sound effects. Changes the lamp's lit state and future interaction behavior. Direct use does not itself spend tinderboxes; the player interaction path does. |

### Registered declarations

```cpp
void SetLightVisible(string &in asLightName, bool abVisible);
void FadeLightTo(
    string &in asLightName,
    float afR,
    float afG,
    float afB,
    float afA,
    float afRadius,
    float afTime
);
void SetLightFlickerActive(string& asLightName, bool abActive);
void SetPropEffectActive(string &in asName, bool abActive, bool abFadeAndPlaySounds);
void SetLampLit(string &in asName, bool abLit, bool abEffects);
```

Registration references: [SetLightVisible](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:571) · [FadeLightTo](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:572) · [SetLightFlickerActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:573) · [SetPropEffectActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:593) · [SetLampLit](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:604).

<a id="enemies"></a>

## Enemies & AI

Host-controlled enemy decisions and lifecycle. Player-attributed commands must retain their subject; presentation flags can require separate handling.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `SetEnemyDisabled` | Shared world | Gameplay | Authority changes enemy simulation state and replicates it. Disables the actual shared enemy; hiding its mesh is a separate presentation operation. |
| `SetEnemyIsHallucination` | Shared world | Gameplay | Authority sets hallucination behavior on the shared enemy. Proximity can make the enemy fade out and deactivate for everyone. A private apparition needs a separate client presentation entity. |
| `FadeEnemyToSmoke` | Shared world | Mixed | Authority deactivates the enemy; route disappearance effects to viewers. Creates disappearance effects and calls SetActive(false); it is not a cosmetic-only fade. |
| `SetEnemyDisableTriggers` | Shared world | Gameplay | Authority controls which stimuli can drive the enemy. Changes the shared AI's response policy rather than merely hiding player feedback. |
| `ShowEnemyPlayerPosition` | Shared world | Gameplay | Authority reveals the resolved player to the shared enemy. Uses script-player context, with existing fallback target selection, and drives hunting behavior. The target player must remain explicit through deferred callbacks. |
| `AlertEnemyOfPlayerPresence` | Shared world | Gameplay | Authority reveals the resolved player and updates enemy awareness. Can switch the shared enemy into search behavior; a player-specific input still changes common AI state. |
| `AddEnemyPatrolNode` | Shared world | Gameplay | Authority extends the patrol route and replicates enemy outcomes. Adds a navigation node, wait time and animation to the enemy's shared behavior. |
| `ClearEnemyPatrolNodes` | Shared world | Gameplay | Authority clears the patrol route and replicates enemy outcomes. Clears the enemy's patrol nodes and resets its current patrol index. |
| `SetEnemySanityDecreaseActive` | Shared world | Gameplay | Use an enemy-wide rule or an explicit per-player exposure policy. Currently one enemy flag governs observers' sanity loss. Personal immunity requires distinct per-player policy rather than conflicting replica flags. |
| `TeleportEnemyToNode` | Shared world | Gameplay | Authority teleports the enemy and replicates the new transform. Moves actual enemy feet to a navigation node; optionally retains the current height. |
| `TeleportEnemyToEntity` | Shared world | Gameplay | Authority teleports the enemy and replicates the new transform. Uses another enemy's feet or a target body position; optionally retains the current height. |
| `ChangeManPigPose` | Shared world | Mixed | Authority changes the enemy pose and replicates simulation and animation. Also stops movement, adds a movement delay and sends an AI pose-change message; it is not only a visual pose switch. |
| `SetTeslaPigFadeDisabled` | Presentation | Presentation | Apply Tesla fade policy to the affected player's presentation. Controls screen-fade behavior associated with the Tesla encounter; it need not be identical for every viewer. |
| `SetTeslaPigSoundDisabled` | Presentation | Presentation | Apply Tesla audio policy to selected players. Controls the encounter's local sound and can fade out its current loop; it does not itself change pursuit state. |
| `SetTeslaPigEasyEscapeDisabled` | Shared world | Gameplay | Authority changes the enemy's escape behavior. Affects whether pursuit can relax into a wait state based on target health and awareness. |
| `ForceTeslaPigSighting` | Policy required | Mixed | Coordinate canonical blink state or isolate a private sighting effect. Changes the Tesla enemy's sighting/blink state and mesh visibility; separate local presentation from shared enemy state before targeting it. |
| `GetEnemyStateName` | Shared world | Read | Read authoritative AI for decisions; replica state for presentation. Returns the named enemy's current state without advancing its state machine. |

### Registered declarations

```cpp
void SetEnemyDisabled(string &in asName, bool abDisabled);
void SetEnemyIsHallucination(string &in asName, bool abX);
void FadeEnemyToSmoke(string &in asName, bool abPlaySound);
void SetEnemyDisableTriggers(string &in asName, bool abX);
void ShowEnemyPlayerPosition(string &in asName);
void AlertEnemyOfPlayerPresence(string &in asName);
void AddEnemyPatrolNode(
    string &in asEnemyName,
    string &in asNodeName,
    float afWaitTime,
    string &in asAnimation
);
void ClearEnemyPatrolNodes(string &in asEnemyName);
void SetEnemySanityDecreaseActive(string &in asName, bool abX);
void TeleportEnemyToNode(string &in asEnemyName, string &in asNodeName, bool abChangeY);
void TeleportEnemyToEntity(
    string &in asEnemyName,
    string &in asTargetEntity,
    string &in asTargetBody,
    bool abChangeY
);
void ChangeManPigPose(string&in asName, string&in asPoseType);
void SetTeslaPigFadeDisabled(string&in asName, bool abX);
void SetTeslaPigSoundDisabled(string&in asName, bool abX);
void SetTeslaPigEasyEscapeDisabled(string&in asName, bool abX);
void ForceTeslaPigSighting(string&in asName);
string& GetEnemyStateName(string &in asName);
```

Registration references: [SetEnemyDisabled](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:633) · [SetEnemyIsHallucination](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:634) · [FadeEnemyToSmoke](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:635) · [SetEnemyDisableTriggers](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:636) · [ShowEnemyPlayerPosition](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:637) · [AlertEnemyOfPlayerPresence](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:638) · [AddEnemyPatrolNode](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:639) · [ClearEnemyPatrolNodes](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:640) · [SetEnemySanityDecreaseActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:641) · [TeleportEnemyToNode](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:642) · [TeleportEnemyToEntity](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:643) · [ChangeManPigPose](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:645) · [SetTeslaPigFadeDisabled](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:646) · [SetTeslaPigSoundDisabled](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:647) · [SetTeslaPigEasyEscapeDisabled](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:648) · [ForceTeslaPigSighting](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:649) · [GetEnemyStateName](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:650).

<a id="npc"></a>

## NPC presentation

These NPC APIs primarily control animation and head tracking. Preserve animation callbacks and decide whether appearance is shared or personal.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `SetNPCAwake` | Policy required | Mixed | Choose canonical NPC state or an isolated presentation override. Controls sleep/wake/idle animation and whether head tracking runs. Animation/body or callback dependencies must be checked before treating it as private. |
| `SetNPCFollowPlayer` | Policy required | Mixed | Choose a target player for shared gaze or per-viewer head tracking. Despite the name, this controls head-bone look-at rather than walking. Current targeting uses the local player and follow-area overlap. |

### Registered declarations

```cpp
void SetNPCAwake(string &in asName, bool abAwake, bool abEffects);
void SetNPCFollowPlayer(string &in asName, bool abX);
```

Registration references: [SetNPCAwake](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:630) · [SetNPCFollowPlayer](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:631).

<a id="player_body"></a>

## Player body, movement & control

The host identifies the affected player. Execute the appropriate operation against that character; other peers observe the resulting replicated state.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `SetPlayerActive` | Selected player | Gameplay | Host targets a player; apply the control state and synchronize relevant player state. Enables or disables the selected player. The actor must be explicit; it need not affect everyone. |
| `ChangePlayerStateToNormal` | Selected player | Gameplay | Host targets a player; transition the owning controller consistently. Returns the player's interaction state to normal, which can end an interaction with a shared object. |
| `SetPlayerCrouching` | Selected player | Gameplay | Host targets a player; apply stance and synchronize the resulting body state. Switches to the normal movement state and changes crouching for the selected actor. |
| `AddPlayerBodyForce` | Selected player | Gameplay | Host targets a player; apply force through that player's authorized controller. Changes player-body motion; local-coordinate interpretation uses the target player's body, not the host's. |
| `SetPlayerPos` | Selected player | Gameplay | Host targets a player; apply an authoritative position change and synchronize it. Sets the selected player's feet position. Other peers need the resulting actor state, not the same command applied to themselves. |
| `GetPlayerPosX` | Selected player | Read | Read the selected player's authoritative state or an explicitly identified client snapshot. Returns the target player's feet-position X coordinate; the implicit actor must come from dispatch context. |
| `GetPlayerPosY` | Selected player | Read | Read the selected player's authoritative state or an explicitly identified client snapshot. Returns the target player's feet-position Y coordinate; a client's local value may lag host state. |
| `GetPlayerPosZ` | Selected player | Read | Read the selected player's authoritative state or an explicitly identified client snapshot. Returns the target player's feet-position Z coordinate; do not silently substitute the local host player. |
| `GetPlayerSpeed` | Selected player | Read | Read the selected player's movement state or an identified snapshot. Returns total player-body speed. The current native implementation samples velocity using a fixed 1/60-second interval. |
| `GetPlayerYSpeed` | Selected player | Read | Read the selected player's movement state or an identified snapshot. Returns the selected player's vertical speed; distinguish authoritative movement checks from local presentation reads. |
| `MovePlayerForward` | Selected player | Gameplay | Host authorizes target-player motion; apply through the owning controller and synchronize state. Moves the selected character body forward. Sustained motion should not become a stream of arbitrary script invocations. |
| `SetPlayerMoveSpeedMul` | Selected player | Gameplay | Host targets a player; synchronize the movement modifier with the owning controller. Changes the selected player's scripted movement-speed multiplier. |
| `SetPlayerRunSpeedMul` | Selected player | Gameplay | Host targets a player; synchronize the run-speed modifier. Changes the selected player's running-speed multiplier. |
| `SetPlayerJumpForceMul` | Selected player | Gameplay | Host targets a player; synchronize the jump modifier with movement authority. Changes the selected player's jump-force multiplier and resulting physical motion. |
| `SetPlayerJumpDisabled` | Selected player | Gameplay | Host targets a player; synchronize and enforce the jump restriction. Disables or enables jumping for the selected actor. |
| `SetPlayerCrouchDisabled` | Selected player | Gameplay | Host targets a player; synchronize and enforce the crouch restriction. Disables or enables crouching for the selected actor. |
| `TeleportPlayer` | Selected player | Gameplay | Host targets a player; resolve the destination and synchronize the teleport. Places the selected player at a named start node. The other players should observe that actor's new position. |

### Registered declarations

```cpp
void SetPlayerActive(bool abActive);
void ChangePlayerStateToNormal();
void SetPlayerCrouching(bool abCrouch);
void AddPlayerBodyForce(float afX, float afY, float afZ, bool abUseLocalCoords);
void SetPlayerPos(float afX, float afY, float afZ);
float GetPlayerPosX();
float GetPlayerPosY();
float GetPlayerPosZ();
float GetPlayerSpeed();
float GetPlayerYSpeed();
void MovePlayerForward(float afAmount);
void SetPlayerMoveSpeedMul(float afMul);
void SetPlayerRunSpeedMul(float afMul);
void SetPlayerJumpForceMul(float afMul);
void SetPlayerJumpDisabled(bool abX);
void SetPlayerCrouchDisabled(bool abX);
void TeleportPlayer(string &in asStartPosName);
```

Registration references: [SetPlayerActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:469) · [ChangePlayerStateToNormal](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:470) · [SetPlayerCrouching](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:471) · [AddPlayerBodyForce](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:472) · [SetPlayerPos](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:475) · [GetPlayerPosX](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:476) · [GetPlayerPosY](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:477) · [GetPlayerPosZ](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:478) · [GetPlayerSpeed](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:490) · [GetPlayerYSpeed](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:491) · [MovePlayerForward](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:492) · [SetPlayerMoveSpeedMul](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:509) · [SetPlayerRunSpeedMul](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:510) · [SetPlayerJumpForceMul](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:512) · [SetPlayerJumpDisabled](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:513) · [SetPlayerCrouchDisabled](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:514) · [TeleportPlayer](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:517).

<a id="player_vitals"></a>

## Health, sanity & lantern

Player-owned gameplay state. Targeting one character is distinct from applying an operation to every character.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `SetInDarknessEffectsActive` | Selected player | Mixed | Host targets a player; synchronize the gameplay setting and apply local effects. Toggles the entire darkness helper, including darkness status and sanity drain. Disabling it resets the sanity-loss multiplier; it is not merely visual. |
| `SetPlayerSanity` | Selected player | Gameplay | Host targets a player; commit the sanity value and synchronize the result. Sets one player's sanity. Any associated presentation belongs to that player. |
| `AddPlayerSanity` | Selected player | Gameplay | Host targets a player; apply the sanity delta once and synchronize the result. Adds to the target player's sanity; repeated delivery must not apply the delta twice. |
| `GetPlayerSanity` | Selected player | Read | Read the selected player's authoritative value or an identified snapshot. Queries the target player's sanity; it is not inherently session-wide. |
| `SetPlayerHealth` | Selected player | Gameplay | Host targets a player; commit health and synchronize the result. Sets the selected player's health. Death and recovery behavior must use the same actor. |
| `AddPlayerHealth` | Selected player | Gameplay | Host targets a player; apply the health delta once and synchronize the result. Adds to the selected player's health; avoid independently applying the delta in multiple authority paths. |
| `GetPlayerHealth` | Selected player | Read | Read the selected player's authoritative health or an identified snapshot. Queries one player's health. Client-side reads are observations, not permission to decide shared damage outcomes. |
| `SetPlayerLampOil` | Selected player | Gameplay | Host targets a player; commit oil and synchronize the result. Sets the selected player's lantern fuel, which can affect lantern availability. |
| `AddPlayerLampOil` | Selected player | Gameplay | Host targets a player; apply the fuel delta once and synchronize the result. Adds lantern oil to the selected player's resource state. |
| `GetPlayerLampOil` | Selected player | Read | Read the selected player's fuel state or an identified snapshot. Returns lantern oil for the actor selected by the script context. |
| `SetSanityDrainDisabled` | Selected player | Gameplay | Host targets a player; synchronize the sanity-drain setting. Changes whether the target player loses sanity through normal drain behavior. |
| `GiveSanityBoost` | Selected player | Gameplay | Host targets a player; calculate the boost once from that player's current sanity. The boost amount depends on existing sanity, so evaluating it independently against stale client values can diverge. |
| `GiveSanityBoostSmall` | Selected player | Gameplay | Host targets a player; calculate the smaller boost once and synchronize the result. Adds a sanity-dependent amount to the target player, rather than a constant shared increment. |
| `GiveSanityDamage` | Selected player | Mixed | Host targets a player; commit sanity damage once and send any personal effect. Lowers sanity and optionally starts its effect. Separate the gameplay result from its presentation. |
| `GivePlayerDamage` | Selected player | Mixed | Host targets a player; commit damage once and deliver the associated personal effect. Applies damage with type, head-spin and lethality options; it is not merely a screen-damage effect. |
| `SetPlayerFallDamageDisabled` | Selected player | Gameplay | Host targets a player; synchronize the fall-damage rule. Changes fall-damage eligibility for the selected player, even though the movement itself can remain unchanged. |
| `SetLanternActive` | Selected player | Mixed | Host targets a player; synchronize lantern state and apply personal activation effects. Lantern state affects visibility and gameplay as well as local audio/visual presentation. |
| `GetLanternActive` | Selected player | Read | Read the selected player's lantern state or an identified snapshot. Queries whether the target player's lantern helper is active. |
| `SetLanternDisabled` | Selected player | Gameplay | Host targets a player; synchronize and enforce the lantern restriction. Controls whether that player can use the lantern; it is distinct from currently switching it on or off. |

### Registered declarations

```cpp
void SetInDarknessEffectsActive(bool abX);
void SetPlayerSanity(float afSanity);
void AddPlayerSanity(float afSanity);
float GetPlayerSanity();
void SetPlayerHealth(float afHealth);
void AddPlayerHealth(float afHealth);
float GetPlayerHealth();
void SetPlayerLampOil(float afOil);
void AddPlayerLampOil(float afOil);
float GetPlayerLampOil();
void SetSanityDrainDisabled(bool abX);
void GiveSanityBoost();
void GiveSanityBoostSmall();
void GiveSanityDamage(float afAmount, bool abUseEffect);
void GivePlayerDamage(float afAmount, string &in asType, bool abSpinHead, bool abLethal);
void SetPlayerFallDamageDisabled(bool abX);
void SetLanternActive(bool abX, bool abUseEffects);
bool GetLanternActive();
void SetLanternDisabled(bool abX);
```

Registration references: [SetInDarknessEffectsActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:449) · [SetPlayerSanity](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:480) · [AddPlayerSanity](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:481) · [GetPlayerSanity](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:482) · [SetPlayerHealth](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:483) · [AddPlayerHealth](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:484) · [GetPlayerHealth](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:485) · [SetPlayerLampOil](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:486) · [AddPlayerLampOil](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:487) · [GetPlayerLampOil](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:488) · [SetSanityDrainDisabled](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:495) · [GiveSanityBoost](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:496) · [GiveSanityBoostSmall](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:497) · [GiveSanityDamage](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:498) · [GivePlayerDamage](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:500) · [SetPlayerFallDamageDisabled](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:515) · [SetLanternActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:518) · [GetLanternActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:519) · [SetLanternDisabled](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:520).

<a id="inventory"></a>

## Inventory & item use

Inventory ownership and item rewards need a player or party policy. Personal interface operations execute for the intended recipient.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `ExitInventory` | Presentation | Presentation | Host targets a player; close that player's inventory UI if open. Exits the local inventory screen. It does not remove items or alter other players' inventory screens. |
| `SetInventoryDisabled` | Selected player | Gameplay | Host targets a player; synchronize and enforce the inventory-use restriction. Disables inventory access for the selected actor; this can restrict gameplay even though it is implemented through the UI. |
| `SetInventoryMessage` | Presentation | Presentation | Host selects recipients; display the message in their inventory UI. Shows translated inventory-message text without mutating inventory contents. |
| `GiveItem` | Selected player | Gameplay | Host grants to the selected inventory once; synchronize ownership and personal feedback. Adds an item with explicit type and amount. A shared-inventory mode must define a different ownership policy explicitly. |
| `GiveItemFromFile` | Selected player | Gameplay | Host grants to the selected inventory once; isolate resource loading from world replication. Currently loads a temporary map entity to extract item data, then destroys it. Keep that loading mechanism from becoming an unintended shared entity spawn. |
| `RemoveItem` | Selected player | Gameplay | Host removes from the selected inventory once; synchronize ownership. Removes the named item from the actor's inventory. Do not automatically remove it from every player. |
| `HasItem` | Selected player | Read | Read the selected inventory's authoritative state or an identified synchronized copy. Tests possession for the actor selected by the script context, subject to any explicit shared-inventory policy. |

### Registered declarations

```cpp
void ExitInventory();
void SetInventoryDisabled(bool abX);
void SetInventoryMessage(string &in asTextCategory, string &in asTextEntry, float afTime);
void GiveItem(
    string &in asName,
    string &in asType,
    string &in asSubTypeName,
    string &in asImageName,
    float afAmount
);
void GiveItemFromFile(string& asName, string& asFileName);
void RemoveItem(string &in asName);
bool HasItem(string &in asName);
```

Registration references: [ExitInventory](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:541) · [SetInventoryDisabled](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:542) · [SetInventoryMessage](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:543) · [GiveItem](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:545) · [GiveItemFromFile](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:546) · [RemoveItem](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:547) · [HasItem](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:548).

<a id="progression"></a>

## Quests, journals & progress

Choose shared campaign progress or personal objectives deliberately. Existing functions sometimes combine journal presentation, map completion and achievements.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `AddNote` | Policy required | Mixed | Host selects personal or shared journal ownership; grant once and update relevant players. Adds journal content. Define whether collection belongs to one player or the party before routing grants and progress effects. |
| `AddDiary` | Policy required | Mixed | Host selects personal or shared journal ownership; grant once and update relevant players. Adds a diary to the journal. Ownership and any associated presentation must follow the collecting actor or the chosen party policy. |
| `AddQuest` | Policy required | Mixed | Host applies the chosen quest-ownership policy once; send state and personal notifications. Adds a quest note and UI feedback. Decide whether quest ownership is personal or shared rather than deriving it from the local journal singleton. |
| `CompleteQuest` | Policy required | Mixed | Host commits completion once; synchronize progress and notify the intended players. Combines journal completion, map-completion accounting and some achievements. Split authoritative progress from personal presentation. |
| `QuestIsCompleted` | Policy required | Read | Read the chosen personal or shared quest state; clients may inspect synchronized copies. Queries the journal's quest status. The result depends on the ownership policy and actor context. |
| `QuestIsAdded` | Policy required | Read | Read the chosen personal or shared quest state; clients may inspect synchronized copies. Tests whether a quest note exists in the relevant journal; absence and completion are separate states. |
| `SetNumberOfQuestsInMap` | Shared world | Gameplay | Host sets the map-level quest count; synchronize it with shared progress state. Writes a property of the current map, not an individual journal or UI counter. |

### Registered declarations

```cpp
void AddNote(string &in asNameAndTextEntry, string &in asImage);
void AddDiary(string &in asNameAndTextEntry, string &in asImage);
void AddQuest(string &in asName, string &in asNameAndTextEntry);
void CompleteQuest(string &in asName, string &in asNameAndTextEntry);
bool QuestIsCompleted(string &in asName);
bool QuestIsAdded(string &in asName);
void SetNumberOfQuestsInMap(int alNumberOfQuests);
```

Registration references: [AddNote](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:526) · [AddDiary](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:527) · [AddQuest](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:530) · [CompleteQuest](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:531) · [QuestIsCompleted](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:532) · [QuestIsAdded](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:533) · [SetNumberOfQuestsInMap](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:534).

<a id="camera"></a>

## Camera & screen effects

Host-directed presentation for a selected player or audience. Completion callbacks must retain the originating player and execution domain.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `FadeIn` | Presentation | Presentation | Host selects recipients; run each fade locally. Fades the selected player's screen in; the visual progression needs no per-frame network calls. |
| `FadeOut` | Presentation | Presentation | Host selects recipients; run each fade locally. Fades the selected player's screen out. It does not itself pause or disable the player. |
| `FadeImageTrailTo` | Presentation | Presentation | Host selects recipients; interpolate locally. Changes the image-trail post effect for the selected view. |
| `FadeSepiaColorTo` | Presentation | Presentation | Host selects recipients; interpolate locally. Changes the selected view's sepia post effect. |
| `FadeRadialBlurTo` | Presentation | Presentation | Host selects recipients; interpolate locally. Changes radial blur amount and speed for the selected view. |
| `SetRadialBlurStartDist` | Presentation | Presentation | Host selects recipients; apply to their local views. Sets the radial-blur start distance; no shared world mutation is required. |
| `StartEffectFlash` | Presentation | Presentation | Host selects recipients; animate locally. Starts a screen flash. Treat the flash timeline as local presentation. |
| `StartScreenShake` | Presentation | Presentation | Host selects recipients; animate locally. Starts a screen shake for selected views. Shared event timing does not require networking every shake update. |
| `FadePlayerFOVMulTo` | Presentation | Presentation | Host selects recipients; interpolate their camera FOV locally. Changes the selected player's field-of-view multiplier. |
| `FadePlayerAspectMulTo` | Presentation | Presentation | Host selects recipients; interpolate their camera aspect locally. Changes the selected view's aspect multiplier for a visual distortion. |
| `FadePlayerRollTo` | Presentation | Presentation | Host selects recipients; animate camera roll locally. Rolls the selected player's view; native input uses degrees and converts to radians internally. |
| `MovePlayerHeadPos` | Presentation | Presentation | Host selects recipients; animate the scripted head offset locally. Changes the scripted head/camera offset rather than teleporting the character body. |
| `StartPlayerLookAt` | Selected player | Mixed | Host targets a player; steer that view locally and route completion to the callback owner. Looks toward an entity and can invoke a completion callback. Preserve actor context and callback realm; do not rerun shared logic on every viewer. |
| `StopPlayerLookAt` | Selected player | Mixed | Host targets the affected player; stop that player's look-at helper. Stops scripted view control for the selected player; coordinate it with the corresponding look-at operation and callback. |
| `SetPlayerLookSpeedMul` | Selected player | Gameplay | Host targets a player; configure that player's look controls. Changes player look-input speed; it is a control setting rather than a view-only post effect. |

### Registered declarations

```cpp
void FadeIn(float afTime);
void FadeOut(float afTime);
void FadeImageTrailTo(float afAmount, float afSpeed);
void FadeSepiaColorTo(float afAmount, float afSpeed);
void FadeRadialBlurTo(float afSize, float afSpeed);
void SetRadialBlurStartDist(float afStartDist);
void StartEffectFlash(float afFadeIn, float afWhite, float afFadeOut);
void StartScreenShake(float afAmount, float afTime, float afFadeInTime, float afFadeOutTime);
void FadePlayerFOVMulTo(float afX, float afSpeed);
void FadePlayerAspectMulTo(float afX, float afSpeed);
void FadePlayerRollTo(float afX, float afSpeedMul, float afMaxSpeed);
void MovePlayerHeadPos(float afX, float afY, float afZ, float afSpeed, float afSlowDownDist);
void StartPlayerLookAt(
    string &in asEntityName,
    float afSpeedMul,
    float afMaxSpeed,
    string &in asAtTargetCallback
);
void StopPlayerLookAt();
void SetPlayerLookSpeedMul(float afMul);
```

Registration references: [FadeIn](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:439) · [FadeOut](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:440) · [FadeImageTrailTo](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:441) · [FadeSepiaColorTo](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:442) · [FadeRadialBlurTo](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:443) · [SetRadialBlurStartDist](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:444) · [StartEffectFlash](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:446) · [StartScreenShake](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:455) · [FadePlayerFOVMulTo](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:501) · [FadePlayerAspectMulTo](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:502) · [FadePlayerRollTo](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:503) · [MovePlayerHeadPos](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:504) · [StartPlayerLookAt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:506) · [StopPlayerLookAt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:507) · [SetPlayerLookSpeedMul](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:511).

<a id="interface"></a>

## Messages, hints & interface

Presentation and control for the intended recipient. A shared callback may intentionally direct the same operation to everyone.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `SetupLoadScreen` | Presentation | Presentation | Host selects recipients; configure each recipient's loading UI. Sets loading text and image choices. A common map transition can target everyone without making this gameplay state. |
| `ShowPlayerCrossHairIcons` | Presentation | Presentation | Host selects recipients; change their local HUD. Controls focus icons and the crosshair for selected players without changing entity interaction permissions. |
| `SetMessage` | Presentation | Presentation | Host selects recipients; display the message locally. Shows translated text to selected players for the supplied duration. |
| `SetDeathHint` | Presentation | Presentation | Host targets a player; configure that player's death hint. Changes the personal death-hint text without changing the map checkpoint. |
| `ReturnOpenJournal` | Script context | Presentation | Return the journal-opening decision for the current pickup actor and callback. Controls whether a diary pickup opens the journal. Preserve the originating pickup context; this is not a session-wide UI setting. |
| `GiveHint` | Presentation | Presentation | Host selects recipients; display and time their hints locally. Adds a named hint to selected players' hint handlers. |
| `RemoveHint` | Presentation | Presentation | Host selects recipients; remove the hint from their local UI. Removes the named hint for selected players without changing the underlying puzzle state. |
| `BlockHint` | Presentation | Presentation | Host selects recipients; update their personal hint filters. Blocks the named hint in the selected player's hint handler. |
| `UnBlockHint` | Presentation | Presentation | Host selects recipients; update their personal hint filters. Allows the named hint again for selected players. |

### Registered declarations

```cpp
void SetupLoadScreen(
    string &in asTextCat,
    string &in asTextEntry,
    int alRandomNum,
    string &in asImageFile
);
void ShowPlayerCrossHairIcons(bool abX);
void SetMessage(string &in asTextCategory, string &in asTextEntry, float afTime);
void SetDeathHint(string &in asTextCategory, string &in asTextEntry);
void ReturnOpenJournal(bool abOpenJournal);
void GiveHint(string &in asName, string &in asMessageCat, string &in asMessageEntry, float afTimeShown);
void RemoveHint(string &in asName);
void BlockHint(string &in asName);
void UnBlockHint(string &in asName);
```

Registration references: [SetupLoadScreen](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:437) · [ShowPlayerCrossHairIcons](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:473) · [SetMessage](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:522) · [SetDeathHint](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:523) · [ReturnOpenJournal](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:528) · [GiveHint](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:536) · [RemoveHint](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:537) · [BlockHint](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:538) · [UnBlockHint](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:539).

<a id="player_effects"></a>

## Insanity & flashback effects

Personal effects can also change sanity, input or world sound. These functions are not uniformly cosmetic.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `StartEffectEmotionFlash` | Presentation | Presentation | Host selects recipients; play the effect locally. Combines an emotional flash with text and sound for the selected player. |
| `GetFlashbackIsActive` | Presentation | Read | Read the selected player's effect state; report it when host logic requires it. Queries the player's flashback helper, not a single session-wide flashback flag. |
| `SetInsanitySetEnabled` | Selected player | Mixed | Host targets a player; configure that player's insanity-event selection. Controls eligible insanity events. Events can include input restrictions and world sound creation as well as visuals. |
| `StartRandomInsanityEvent` | Selected player | Mixed | Host authorizes a target-player event; distinguish gameplay effects from local presentation. Chooses an insanity event using randomness. If its identity matters to shared logic, choose once and communicate the selected event. |
| `StartInsanityEvent` | Selected player | Mixed | Host targets a player; run the selected event with explicit gameplay and presentation handling. A named insanity event can disable player input or create world sounds; do not classify all such events as cosmetic. |
| `StopCurrentInsanityEvent` | Selected player | Mixed | Host targets the event's player; stop it and restore its affected state. Stops that player's active event. Cleanup may restore input as well as remove audio or visuals. |
| `InsanityEventIsActive` | Selected player | Read | Read the selected player's insanity state; report it to host logic when needed. Tests whether the target player's insanity handler has an active event. See the separate binding-defect note for the registered return type. **Known binding defect; see below.** |

### Registered declarations

```cpp
void StartEffectEmotionFlash(string &in asTextCat, string &in asTextEntry, string &in asSound);
bool GetFlashbackIsActive();
void SetInsanitySetEnabled(string &in asSet, bool abX);
void StartRandomInsanityEvent();
void StartInsanityEvent(string &in asEventName);
void StopCurrentInsanityEvent();
void InsanityEventIsActive();
```

Registration references: [StartEffectEmotionFlash](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:447) · [GetFlashbackIsActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:456) · [SetInsanitySetEnabled](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:458) · [StartRandomInsanityEvent](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:459) · [StartInsanityEvent](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:460) · [StopCurrentInsanityEvent](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:461) · [InsanityEventIsActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:462).

<a id="environment"></a>

## Sky, fog & map presentation

Shared environmental defaults are useful; private render overrides are possible. Current world fields do not themselves establish a multiplayer ownership rule.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `SetMapDisplayNameEntry` | Presentation | Presentation | Apply locally or send the selected display-name value to intended recipients. Normally common map metadata, but the display name need not be a globally executed operation. |
| `SetSkyBoxActive` | Presentation | Presentation | Apply to selected recipients' local rendering state. A common environment can share this value; a personalized sky is also possible. |
| `SetSkyBoxTexture` | Presentation | Presentation | Send the texture choice to selected recipients, then resolve it locally. Rendering state rather than physical world authority; recipients need the referenced texture. |
| `SetSkyBoxColor` | Presentation | Presentation | Apply to selected recipients' local rendering state. May represent a shared environment or a deliberate personal visual effect. |
| `SetFogActive` | Presentation | Presentation | Apply to selected recipients' local rendering state. Fog may be shared atmosphere or a personal effect; no physics ownership requirement. |
| `SetFogColor` | Presentation | Presentation | Apply to selected recipients' local rendering state. Affects rendered fog color rather than authoritative collision or progression. |
| `SetFogProperties` | Presentation | Presentation | Apply to selected recipients' local rendering state. Includes fog culling and visibility distance; keep common when fair/shared visibility is a design requirement. |

### Registered declarations

```cpp
void SetMapDisplayNameEntry(string &in asNameEntry);
void SetSkyBoxActive(bool abActive);
void SetSkyBoxTexture(string &in asTexture);
void SetSkyBoxColor(float afR, float afG, float afB, float afA);
void SetFogActive(bool abActive);
void SetFogColor(float afR, float afG, float afB, float afA);
void SetFogProperties(float afStart, float afEnd, float afFalloffExp, bool abCulling);
```

Registration references: [SetMapDisplayNameEntry](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:426) · [SetSkyBoxActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:427) · [SetSkyBoxTexture](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:428) · [SetSkyBoxColor](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:429) · [SetFogActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:433) · [SetFogColor](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:434) · [SetFogProperties](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:435).

<a id="particles"></a>

## Particles & player-centered weather

Coordinate effect ownership and start/stop decisions as needed. World effects retain their placement; surrounding weather particles are generated locally for the affected player.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `StartPlayerSpawnPS` | Presentation | Presentation | Host selects recipients; start weather locally around each recipient. Starts player-centered particle weather. The existing spawn helper explicitly keeps subsequent particle creation local. |
| `StopPlayerSpawnPS` | Presentation | Presentation | Host selects recipients; stop each local weather spawner. Stops the selected player's weather-spawn helper. See the separate binding-defect note for the current native registration. **Known binding defect; see below.** |
| `CreateParticleSystemAtEntity` | Presentation | Presentation | Present to selected players; broadcast when shared. Creates a named visual effect at an entity or the resolved Player position; saved effects use world placement rather than ordinary attachment. |
| `CreateParticleSystemAtEntityExt` | Presentation | Presentation | Present to selected players; broadcast when shared. Adds color and distance-fade settings; resolve Player explicitly and keep effect names within their owning presentation context. |
| `DestroyParticleSystem` | Presentation | Presentation | Remove from the presentation contexts that own it. Kills particle systems matching the name; private and shared effects need distinct ownership to avoid name collisions. |

### Registered declarations

```cpp
void StartPlayerSpawnPS(string &in asSPSFile);
void StopPlayerSpawnPS();
void CreateParticleSystemAtEntity(
    string &in asPSName,
    string &in asPSFile,
    string &in asEntity,
    bool abSavePS
);
void CreateParticleSystemAtEntityExt(
    string &in asPSName,
    string &in asPSFile,
    string &in asEntity,
    bool abSavePS,
    float afR,
    float afG,
    float afB,
    float afA,
    bool abFadeAtDistance,
    float afFadeMinEnd,
    float afFadeMinStart,
    float afFadeMaxStart,
    float afFadeMaxEnd
);
void DestroyParticleSystem(string &in asName);
```

Registration references: [StartPlayerSpawnPS](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:464) · [StopPlayerSpawnPS](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:465) · [CreateParticleSystemAtEntity](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:559) · [CreateParticleSystemAtEntityExt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:560) · [DestroyParticleSystem](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:561).

<a id="audio"></a>

## Sound, voices & music

Distinguish listener-specific playback from world sounds that can stimulate AI. Named sound operations must retain the correct instance and audience.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `AddEffectVoice` | Presentation | Presentation | Host selects listeners; queue voice and subtitles locally. Queues voice/effect audio and subtitles, optionally positioned at an entity. Positioning does not by itself require all players to hear it. |
| `StopAllEffectVoices` | Presentation | Presentation | Host selects listeners; fade their voice queues locally. Stops the selected recipient's effect voices, rather than every player's voices by definition. |
| `GetEffectVoiceActive` | Presentation | Read | Read the selected listener's local state; report completion when needed. Queries that listener's voice queue. Host logic must not substitute the host's playback state for another player's. |
| `PlayGuiSound` | Presentation | Presentation | Host selects listeners; play GUI audio locally. Plays a GUI sound for selected listeners. Local sample selection need not be identical unless the authored event requires it. |
| `SetPlayerPermaDeathSound` | Presentation | Presentation | Host selects recipients; configure their local death audio. Sets the sound used for the player's permanent-death presentation. |
| `DisableDeathStartSound` | Presentation | Presentation | Host targets a player; suppress that player's death-start audio. Changes death presentation for the selected player, not death eligibility. |
| `PlaySoundAtEntity` | Policy required | Mixed | Target private audio; let authority emit shared audible events. Player uses GUI playback, while other entities create spatial sound. Hearable spatial sounds can stimulate enemy AI, so private audio must suppress that stimulus. |
| `FadeInSound` | Policy required | Mixed | Route to the sound owner and intended listeners. Starts or fades an existing named sound; playback can trigger enemy-hearing callbacks unless designated presentation-only. |
| `StopSound` | Policy required | Mixed | Route to the sound owner and intended listeners. Stops or fades matching sound entities. Coordinate the lifetime of shared world audio; private playback may stop independently. |
| `PlayMusic` | Presentation | Presentation | Play on selected players; broadcast for a common score. Controls a local music channel with priority, fade, loop and resume behavior; it does not move or control an enemy. |
| `StopMusic` | Presentation | Presentation | Stop on selected players; broadcast when desired. Stops the music at the supplied priority using the requested fade. |
| `FadeGlobalSoundVolume` | Presentation | Presentation | Apply to each selected player's audio mixer. Global means that process's sound mix, not mandatory session-wide delivery. |
| `FadeGlobalSoundSpeed` | Presentation | Presentation | Apply to each selected player's audio mixer. Changes the local sound mix's playback speed; use private delivery for subjective time or sanity effects. |

### Registered declarations

```cpp
void AddEffectVoice(
    string &in asVoiceFile,
    string &in asEffectFile,
    string &in asTextCat,
    string &in asTextEntry,
    bool abUsePostion,
    string &in asPosEnitity,
    float afMinDistance,
    float afMaxDistance
);
void StopAllEffectVoices(float afFadeOutTime);
bool GetEffectVoiceActive();
void PlayGuiSound(string &in asSoundFile, float afVolume);
void SetPlayerPermaDeathSound(string &in asSound);
void DisableDeathStartSound();
void PlaySoundAtEntity(
    string &in asSoundName,
    string &in asSoundFile,
    string &in asEntity,
    float afFadeSpeed,
    bool abSaveSound
);
void FadeInSound(string& asSoundName, float afFadeTime, bool abPlayStart);
void StopSound(string &in asSoundName, float afFadeTime);
void PlayMusic(
    string &in asMusicFile,
    bool abLoop,
    float afVolume,
    float afFadeTime,
    int alPrio,
    bool abResume
);
void StopMusic(float afFadeTime, int alPrio);
void FadeGlobalSoundVolume(float afDestVolume, float afTime);
void FadeGlobalSoundSpeed(float afDestSpeed, float afTime);
```

Registration references: [AddEffectVoice](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:451) · [StopAllEffectVoices](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:452) · [GetEffectVoiceActive](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:453) · [PlayGuiSound](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:467) · [SetPlayerPermaDeathSound](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:493) · [DisableDeathStartSound](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:524) · [PlaySoundAtEntity](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:563) · [FadeInSound](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:564) · [StopSound](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:565) · [PlayMusic](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:566) · [StopMusic](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:567) · [FadeGlobalSoundVolume](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:568) · [FadeGlobalSoundSpeed](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:569).

<a id="account"></a>

## Account & achievements

Account-specific effects may follow shared gameplay events. The receiving account, rather than the host account by default, is the subject.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `UnlockAchievement` | Selected player | Mixed | Apply to the explicitly eligible player's account; authorize shared-progression awards on the host. Account achievement and notification, not a world mutation or automatic award to every peer. |

### Registered declarations

```cpp
void UnlockAchievement(string &in asName);
```

Registration references: [UnlockAchievement](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:431).

<a id="timers"></a>

## Timers & deferred execution

Scheduling belongs to a script domain. Carry module, map generation and optional player context into delayed work.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `AddTimer` | Script context | Binding | Schedule in the selected server or client domain. Store callback module, domain and any originating player; existing timers only record name, function, remaining time and removal state. |
| `RemoveTimer` | Script context | Binding | Remove the named timer in the selected domain. Server and client timers need separate namespaces, including on a listen host. |
| `GetTimerTimeLeft` | Script context | Read | Read the selected domain's timer. A local remaining-time query does not synchronize clocks or authoritative deadlines. |

### Registered declarations

```cpp
void AddTimer(string &in asName, float afTime, string &in asFunction);
void RemoveTimer(string &in asName);
float GetTimerTimeLeft(string &in asName);
```

Registration references: [AddTimer](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:386) · [RemoveTimer](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:387) · [GetTimerTimeLeft](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:388).

<a id="callbacks"></a>

## Callback bindings

Registrations retain their domain, module, event ownership and optional player. Binding a callback is different from executing its gameplay consequences.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `SetEffectVoiceOverCallback` | Script context | Binding | Bind the callback to its script realm and listener; route completion accordingly. Registers a voice-completion callback. Preserve the originating player and callback owner when playback ends. |
| `SetLanternLitCallback` | Script context | Binding | Bind realm and actor context; deliver the lantern event to its designated callback owner. Currently stores a callback on the map. A targeted design must retain which player's lantern caused the event. |
| `AddCombineCallback` | Script context | Binding | Bind to the intended inventory and script realm; retain the combining player on dispatch. Registers item-combination handling. Authoritative grants and removals inside the callback must execute once for the proper actor. |
| `RemoveCombineCallback` | Script context | Binding | Remove the binding from its original inventory and script realm. Intended to unregister the named combination callback for its original owner. Existing implementation defect: LuxInventory.cpp:1301 compares the iterator against begin(), so the removal loop never runs; the engine must be corrected before relying on removal. |
| `AddUseItemCallback` | Script context | Binding | Register authoritative item-use handling; preserve actor, target entity and callback realm. Currently stores a map callback matching an item and entity. An item-use request must identify the player before gameplay effects run. |
| `RemoveUseItemCallback` | Script context | Binding | Remove the binding from its original map and script realm. Unregisters a named item-use callback. Registration ownership and removal lifetime must remain consistent. |
| `SetEntityPlayerLookAtCallback` | Script context | Binding | Register on the chosen observer context; authority handles world effects. Preserve the observing player and whether one-shot removal applies per player or to the whole session. |
| `SetEntityPlayerInteractCallback` | Script context | Binding | Validate interaction on authority; dispatch with the initiating player. Registration also advertises interactability. Define per-player versus session-wide removal separately from callback execution scope. |
| `SetEntityCallbackFunc` | Script context | Binding | Register with the entity event owner; dispatch declared callback context. Receives native entity events such as pickup or ignition; world state changes remain authoritative even when presentation is targeted. |
| `SetEntityConnectionStateChangeCallback` | Script context | Binding | Run mechanism notifications from authority; target presentation explicitly. The callback observes a shared connection-state change; callback scope does not change the mechanism's ownership. |
| `SetMultiSliderCallback` | Script context | Binding | Register on the mechanism owner; dispatch with event context. Observes slider state changes. Callback execution scope must be separate from the shared slider's physical authority. |
| `AddEntityCollideCallback` | Script context | Binding | Bind collision events to authority or an explicit player observer. Player is a special parent target. Current multiplayer callbacks use combined player occupancy: first entrance and final exit. Decide explicitly whether to introduce per-player events, and whether one-shot deletion applies per player or session-wide; preserve the triggering player identity. |
| `RemoveEntityCollideCallback` | Script context | Binding | Remove from the same event-owner context used for registration. The Player special case and callback owner must resolve consistently with AddEntityCollideCallback. |

### Registered declarations

```cpp
void SetEffectVoiceOverCallback(string &in asFunc);
void SetLanternLitCallback(string &in asCallback);
void AddCombineCallback(
    string &in asName,
    string &in asItemA,
    string &in asItemB,
    string &in asFunction,
    bool abAutoDestroy
);
void RemoveCombineCallback(string &in asName);
void AddUseItemCallback(
    string &in asName,
    string &in asItem,
    string &in asEntity,
    string &in asFunction,
    bool abAutoDestroy
);
void RemoveUseItemCallback(string &in asName);
void SetEntityPlayerLookAtCallback(string &in asName, string &in asCallback, bool abRemoveWhenLookedAt);
void SetEntityPlayerInteractCallback(
    string &in asName,
    string &in asCallback,
    bool abRemoveOnInteraction
);
void SetEntityCallbackFunc(string &in asName, string &in asCallback);
void SetEntityConnectionStateChangeCallback(string& asName, string& asCallback);
void SetMultiSliderCallback(string &in asName, string &in asCallback);
void AddEntityCollideCallback(
    string &in asParentName,
    string &in asChildName,
    string &in asFunction,
    bool abDeleteOnCollide,
    int alStates
);
void RemoveEntityCollideCallback(string &in asParentName, string &in asChildName);
```

Registration references: [SetEffectVoiceOverCallback](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:454) · [SetLanternLitCallback](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:521) · [AddCombineCallback](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:550) · [RemoveCombineCallback](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:551) · [AddUseItemCallback](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:553) · [RemoveUseItemCallback](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:554) · [SetEntityPlayerLookAtCallback](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:586) · [SetEntityPlayerInteractCallback](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:587) · [SetEntityCallbackFunc](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:588) · [SetEntityConnectionStateChangeCallback](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:589) · [SetMultiSliderCallback](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:623) · [AddEntityCollideCallback](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:671) · [RemoveEntityCollideCallback](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:672).

<a id="variables"></a>

## Map & cross-map variables

Legacy LocalVar means current-map lifetime; GlobalVar means cross-map lifetime. Neither name means local player, and neither family automatically replicates today.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `SetLocalVarInt` | Script context | Mixed | Write the selected domain store; legacy shared story state remains host-owned. Replaces the stored value. Local denotes map lifetime, not player identity; client-private state needs a separate store from server state. |
| `SetLocalVarFloat` | Script context | Mixed | Write the selected domain store; legacy shared story state remains host-owned. Replaces the stored value. Local denotes map lifetime, not player identity; client-private state needs a separate store from server state. |
| `SetLocalVarString` | Script context | Mixed | Write the selected domain store; legacy shared story state remains host-owned. Replaces the stored value. Local denotes map lifetime, not player identity; client-private state needs a separate store from server state. |
| `AddLocalVarInt` | Script context | Mixed | Write the selected domain store; legacy shared story state remains host-owned. Adds to the stored numeric value. Local denotes map lifetime, not player identity; client-private state needs a separate store from server state. |
| `AddLocalVarFloat` | Script context | Mixed | Write the selected domain store; legacy shared story state remains host-owned. Adds to the stored numeric value. Local denotes map lifetime, not player identity; client-private state needs a separate store from server state. |
| `AddLocalVarString` | Script context | Mixed | Write the selected domain store; legacy shared story state remains host-owned. Appends text. Local denotes map lifetime, not player identity; client-private state needs a separate store from server state. |
| `GetLocalVarInt` | Script context | Read | Read the selected domain store; shared client reads use an explicit replicated view. Reads the selected store. Local denotes map lifetime, not player identity; client-private state needs a separate store from server state. |
| `GetLocalVarFloat` | Script context | Read | Read the selected domain store; shared client reads use an explicit replicated view. Reads the selected store. Local denotes map lifetime, not player identity; client-private state needs a separate store from server state. |
| `GetLocalVarString` | Script context | Read | Read the selected domain store; shared client reads use an explicit replicated view. Reads the selected store. Local denotes map lifetime, not player identity; client-private state needs a separate store from server state. The current binding returns a mutable reference to the stored string; proposed client read views must use copies or immutable interfaces to enforce read-only access. |
| `SetGlobalVarInt` | Script context | Mixed | Write the selected domain store; legacy shared story state remains host-owned. Replaces the stored value. Global denotes cross-map lifetime, not player identity; client-private state needs a separate store from server state. |
| `SetGlobalVarFloat` | Script context | Mixed | Write the selected domain store; legacy shared story state remains host-owned. Replaces the stored value. Global denotes cross-map lifetime, not player identity; client-private state needs a separate store from server state. |
| `SetGlobalVarString` | Script context | Mixed | Write the selected domain store; legacy shared story state remains host-owned. Replaces the stored value. Global denotes cross-map lifetime, not player identity; client-private state needs a separate store from server state. |
| `AddGlobalVarInt` | Script context | Mixed | Write the selected domain store; legacy shared story state remains host-owned. Adds to the stored numeric value. Global denotes cross-map lifetime, not player identity; client-private state needs a separate store from server state. |
| `AddGlobalVarFloat` | Script context | Mixed | Write the selected domain store; legacy shared story state remains host-owned. Adds to the stored numeric value. Global denotes cross-map lifetime, not player identity; client-private state needs a separate store from server state. |
| `AddGlobalVarString` | Script context | Mixed | Write the selected domain store; legacy shared story state remains host-owned. Appends text. Global denotes cross-map lifetime, not player identity; client-private state needs a separate store from server state. |
| `GetGlobalVarInt` | Script context | Read | Read the selected domain store; shared client reads use an explicit replicated view. Reads the selected store. Global denotes cross-map lifetime, not player identity; client-private state needs a separate store from server state. |
| `GetGlobalVarFloat` | Script context | Read | Read the selected domain store; shared client reads use an explicit replicated view. Reads the selected store. Global denotes cross-map lifetime, not player identity; client-private state needs a separate store from server state. |
| `GetGlobalVarString` | Script context | Read | Read the selected domain store; shared client reads use an explicit replicated view. Reads the selected store. Global denotes cross-map lifetime, not player identity; client-private state needs a separate store from server state. The current binding returns a mutable reference to the stored string; proposed client read views must use copies or immutable interfaces to enforce read-only access. |

### Registered declarations

```cpp
void SetLocalVarInt(string &in asName, int alVal);
void SetLocalVarFloat(string &in asName, float afVal);
void SetLocalVarString(string &in asName, string &in asVal);
void AddLocalVarInt(string &in asName, int alVal);
void AddLocalVarFloat(string &in asName, float afVal);
void AddLocalVarString(string &in asName, string &in asVal);
int GetLocalVarInt(string &in asName);
float GetLocalVarFloat(string &in asName);
string& GetLocalVarString(string &in asName);
void SetGlobalVarInt(string &in asName, int alVal);
void SetGlobalVarFloat(string &in asName, float afVal);
void SetGlobalVarString(string &in asName, string &in asVal);
void AddGlobalVarInt(string &in asName, int alVal);
void AddGlobalVarFloat(string &in asName, float afVal);
void AddGlobalVarString(string &in asName, string &in asVal);
int GetGlobalVarInt(string &in asName);
float GetGlobalVarFloat(string &in asName);
string& GetGlobalVarString(string &in asName);
```

Registration references: [SetLocalVarInt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:390) · [SetLocalVarFloat](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:391) · [SetLocalVarString](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:392) · [AddLocalVarInt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:394) · [AddLocalVarFloat](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:395) · [AddLocalVarString](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:396) · [GetLocalVarInt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:398) · [GetLocalVarFloat](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:399) · [GetLocalVarString](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:400) · [SetGlobalVarInt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:402) · [SetGlobalVarFloat](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:403) · [SetGlobalVarString](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:404) · [AddGlobalVarInt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:406) · [AddGlobalVarFloat](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:407) · [AddGlobalVarString](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:408) · [GetGlobalVarInt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:410) · [GetGlobalVarFloat](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:411) · [GetGlobalVarString](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:412).

<a id="resources"></a>

## Preloading & resource caches

Process-local resource preparation. These caches are distinct from network map caching and authoritative saved-world state.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `CreateDataCache` | Local runtime | Utility | Create the cache in the calling runtime; no gameplay replication. Creates a local model/resource cache, not the multiplayer downloaded-map cache or saved-world state. |
| `DestroyDataCache` | Local runtime | Utility | Destroy the cache in the calling runtime; no gameplay replication. Releases the local model/resource cache; it does not reset map progression. |
| `PreloadParticleSystem` | Local runtime | Utility | Preload in each runtime that will use the resource. Resource preparation only; does not create a particle-system instance. |
| `PreloadSound` | Local runtime | Utility | Preload in each runtime that will play the sound. Resource preparation only; does not start playback. |

### Registered declarations

```cpp
void CreateDataCache();
void DestroyDataCache();
void PreloadParticleSystem(string& asPSFile);
void PreloadSound(string& asSoundFile);
```

Registration references: [CreateDataCache](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:424) · [DestroyDataCache](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:425) · [PreloadParticleSystem](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:556) · [PreloadSound](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:557).

<a id="utility"></a>

## Math, strings, randomness & diagnostics

Utilities have no inherent multiplayer audience. Shared gameplay choices use authoritative randomness; personal effects may use independent local randomness.

| Function | Scope | Impact | Proposed handling |
|---|---|---|---|
| `Print` | Local runtime | Utility | Execute in the calling runtime; no network message. Writes diagnostic output; does not change shared game state. |
| `AddDebugMessage` | Local runtime | Utility | Display in the calling runtime; no network message. Debug overlay message; duplicate filtering is local. |
| `ProgLog` | Local runtime | Utility | Write the calling runtime's progress log. Diagnostic logging is separate from authoritative campaign progress. |
| `ScriptDebugOn` | Local runtime | Read | Read the calling runtime's debug setting. Do not assume every participant has identical debug settings. |
| `RandFloat` | Local runtime | Utility | Generate locally; replicate an authoritative result when gameplay depends on it. PRNG call, not a pure function; separate server and client streams avoid cross-domain interference. |
| `RandInt` | Local runtime | Utility | Generate locally; replicate an authoritative result when gameplay depends on it. PRNG call, not a pure function; matching update rates do not guarantee matching random sequences. |
| `StringContains` | Local runtime | Utility | Evaluate locally; no network message. String operation with no world ownership requirement. |
| `StringSub` | Local runtime | Utility | Evaluate locally; no network message. Returns a reference backed by native temporary string storage; copy results that must survive later calls. |
| `MathSin` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `MathCos` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `MathTan` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `MathAsin` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `MathAcos` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `MathAtan` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `MathAtan2` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `MathSqrt` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `MathPow` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `MathMin` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `MathMax` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `MathClamp` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `MathAbs` | Local runtime | Utility | Evaluate locally; no network message. Pure numeric calculation; validate inputs as appropriate and do not use cross-machine floating-point equality as synchronization. |
| `StringToInt` | Local runtime | Utility | Evaluate locally; no network message. Parses text into a native value; does not read or mutate world state. |
| `StringToFloat` | Local runtime | Utility | Evaluate locally; no network message. Parses text into a native value; does not read or mutate world state. |
| `StringToBool` | Local runtime | Utility | Evaluate locally; no network message. Parses text into a native value; does not read or mutate world state. |

### Registered declarations

```cpp
void Print(string &in asString);
void AddDebugMessage(string &in asString, bool abCheckForDuplicates);
void ProgLog(string &in asLevel, string &in asMessage);
bool ScriptDebugOn();
float RandFloat(float afMin, float afMax);
int RandInt(int alMin, int alMax);
bool StringContains(string &in asString, string &in asSubString);
string& StringSub(string &in asString, int alStart, int alCount);
float MathSin(float afX);
float MathCos(float afX);
float MathTan(float afX);
float MathAsin(float afX);
float MathAcos(float afX);
float MathAtan(float afX);
float MathAtan2(float afX, float afY);
float MathSqrt(float afX);
float MathPow(float afBase, float afExp);
float MathMin(float afA, float afB);
float MathMax(float afA, float afB);
float MathClamp(float afX, float afMin, float afMax);
float MathAbs(float afX);
int StringToInt(string&in asString);
float StringToFloat(string&in asString);
bool StringToBool(string&in asString);
```

Registration references: [Print](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:376) · [AddDebugMessage](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:377) · [ProgLog](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:378) · [ScriptDebugOn](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:379) · [RandFloat](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:381) · [RandInt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:382) · [StringContains](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:383) · [StringSub](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:384) · [MathSin](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:680) · [MathCos](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:681) · [MathTan](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:682) · [MathAsin](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:683) · [MathAcos](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:684) · [MathAtan](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:685) · [MathAtan2](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:686) · [MathSqrt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:687) · [MathPow](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:688) · [MathMin](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:689) · [MathMax](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:690) · [MathClamp](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:691) · [MathAbs](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:692) · [StringToInt](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:694) · [StringToFloat](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:695) · [StringToBool](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:696).

## Engine lifecycle hooks

These entry points are implemented by scripts, rather than registered native functions. They are not included in the 260-function inventory. The proposed ten Server/Client-prefixed variants are not implemented or finalized by this reference.

| Signature | Script context | Current status / design note |
|---|---|---|
| `void OnStart();` | Map script | Existing first-visit initialization. Client initialization for a late join needs its own defined timing. |
| `void OnEnter();` | Map script | Existing map-entry callback. Client state-dependent entry work needs a synchronization-complete point. |
| `void OnLeave();` | Map script | Existing map-exit callback. Complete old-world work before destroying its module and objects. |
| `void OnGameStart();` | Global / inventory script | Existing game-start initialization, not another map-only hook. Both script systems need consistent scope rules. |
| `void OnUpdate(float afStep);` | Proposed map update hook | Documented ATDD 1.5 hook; this checkout does not currently dispatch it into the map script. |

## Open design decisions

- **Story variables.** Preserve the authoritative legacy map/campaign stores. Add explicit player/private stores or per-domain storage rather than treating LocalVar as a player namespace. Replicate only the story values clients actually need, using copied or immutable client views. The current string getters expose mutable stored references.
- **Quests and checkpoints.** Decide party versus personal ownership. Separate shared progression and world-reset callbacks from personal journal UI, achievements, death hints and respawn positions.
- **Presentation overrides.** If private lights, sounds, animations or hallucinations are supported, separate their rendering from gameplay illumination, AI stimuli, collision, callbacks and shared entity lifecycle.
- **Callback lifetime.** Specify once per world versus once per player, removal behavior, map transitions, reconnects and late joins. Current Player collision callbacks use combined occupancy: first entrance and final exit. Per-player enter/leave events need an explicit behavior choice as well as player identity through deferred callbacks.
- **Lifecycle prefixes.** Shared, Server and Client names remain a proposal. Client work is host-triggered and associated with a selected character; it is not autonomous local trigger authority. Define the remaining shared/server distinction before implementing dispatch.
- **Local update modules.** OnUpdate needs a typed dispatch path. Client update code and state must be ready before local ticks begin; the host need not send one invocation per tick. Continuous changes must not fill the existing late-join effect history.

## Known binding defects

These are existing code issues, not changes applied to the engine.

- **`InsanityEventIsActive`** — Existing binding defect: the script declaration returns void, while the native implementation returns bool. The intended corrected declaration is bool InsanityEventIsActive();. The registered declaration is preserved here until the engine binding is fixed. See [registration 462](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:462).
- **`StopPlayerSpawnPS`** — Existing binding defect: StopPlayerSpawnPS() is registered to StartPlayerSpawnPS instead of StopPlayerSpawnPS. The intended signature is already correct; the native binding target needs correction. See [registration 465](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:465).

## Source notes

- [Active native API](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:374) — `amnesia/src/game/LuxScriptHandler.cpp:374`.
- [Client map-script loading guard](D:/Amnesia64/amnesia/src/game/LuxMap.cpp:173) — `amnesia/src/game/LuxMap.cpp:173`.
- [Current native-effect broadcast scope](D:/Amnesia64/amnesia/src/game/LuxMultiplayerScript.h:18) — `amnesia/src/game/LuxMultiplayerScript.h:18`.
- [Late-join effect history](D:/Amnesia64/amnesia/src/game/LuxMultiplayer.cpp:815) — `amnesia/src/game/LuxMultiplayer.cpp:815`.
- [Player getters use the local player today](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:1492) — `amnesia/src/game/LuxScriptHandler.cpp:1492`.
- [Map-local variable storage](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:810) — `amnesia/src/game/LuxScriptHandler.cpp:810`.
- [Cross-map variable storage](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:914) — `amnesia/src/game/LuxScriptHandler.cpp:914`.
- [Mutable string getter references](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:898) — `amnesia/src/game/LuxScriptHandler.cpp:898`.
- [Combined player collision occupancy](D:/Amnesia64/amnesia/src/game/LuxTypes.cpp:173) — `amnesia/src/game/LuxTypes.cpp:173`.
- [Existing combination-removal loop defect](D:/Amnesia64/amnesia/src/game/LuxInventory.cpp:1301) — `amnesia/src/game/LuxInventory.cpp:1301`.
- [Prop activation also affects physics](D:/Amnesia64/amnesia/src/game/LuxProp.cpp:1012) — `amnesia/src/game/LuxProp.cpp:1012`.
- [Gameplay light-level calculation](D:/Amnesia64/amnesia/src/game/LuxMapHelper.cpp:548) — `amnesia/src/game/LuxMapHelper.cpp:548`.
- [World sound and enemy hearing](D:/Amnesia64/amnesia/src/game/LuxMapHandler.cpp:74) — `amnesia/src/game/LuxMapHandler.cpp:74`.
- [Darkness gameplay state](D:/Amnesia64/amnesia/src/game/LuxPlayerHelpers.cpp:3057) — `amnesia/src/game/LuxPlayerHelpers.cpp:3057`.
- [Player-centered particle local scope](D:/Amnesia64/amnesia/src/game/LuxPlayerHelpers.cpp:576) — `amnesia/src/game/LuxPlayerHelpers.cpp:576`.
- [Quest completion combines responsibilities](D:/Amnesia64/amnesia/src/game/LuxScriptHandler.cpp:1829) — `amnesia/src/game/LuxScriptHandler.cpp:1829`.
- [Inventory-script execution](D:/Amnesia64/amnesia/src/game/LuxInventory.cpp:648) — `amnesia/src/game/LuxInventory.cpp:648`.

The input contained 261 declaration lines and 260 unique names. The duplicate `FadeInSound` entry has been removed. Cross-checking against the active registrations found no missing or extra native function names. This reference does not establish complete multiplayer support for every listed operation.
