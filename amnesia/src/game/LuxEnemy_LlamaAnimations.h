/* Enemy_Llama animation presets. GPL-3.0-or-later. */
#ifndef LUX_ENEMY_LLAMA_ANIMATIONS_H
#define LUX_ENEMY_LLAMA_ANIMATIONS_H
#include <cstddef>

// Clip names, relative files, playback settings and ordered events extracted
// from the user-provided entity files. See Enemy_Llama.md for provenance.
// The ModelEditor applies these only through the explicit rig preset action.
// Runtime animation loading continues to use ModelData in the saved entity.
struct cLuxLlamaAnimationEvent
{
    float mfTime;
    const char* msType;
    const char* msValue;
};

struct cLuxLlamaAnimation
{
    const char* msName;
    const char* msFile;
    float mfSpeed;
    float mfSpecialEventTime;
    const cLuxLlamaAnimationEvent* mpEvents;
    std::size_t mlEventCount;
};

struct cLuxLlamaAnimationProfile
{
    const char* msName;
    const cLuxLlamaAnimation* mpAnimations;
    std::size_t mlAnimationCount;
};

static const cLuxLlamaAnimationEvent kLuxLlamaGruntRunEvents[] =
{
    { 0.2f, "PlaySound", "grunt/leather_run" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaGruntWalkEvents[] =
{
    { 0.5f, "Step", "" },
    { 0.5f, "PlaySound", "grunt/leather_walk" },
    { 1.5f, "Step", "" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaGruntSwingClaws01Events[] =
{
    { 0.4f, "PlaySound", "grunt/attack_claw" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaGruntSwingClaws02Events[] =
{
    { 0.4f, "PlaySound", "grunt/attack_claw" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaGruntSwingLaunchEvents[] =
{
    { 0.3f, "Step", "" },
    { 0.2f, "PlaySound", "grunt/attack_launch" },
    { 1.0f, "Step", "" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaGruntBreakDoorEvents[] =
{
    { 0.5f, "PlaySound", "grunt/attack_launch" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaGruntFlinchEvents[] =
{
    { 0.1f, "PlaySound", "grunt/amb_idle_scratch" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaGruntIdleExtra1Events[] =
{
    { 0.4f, "PlaySound", "grunt/amb_idle_scratch" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaGruntIdleExtra2Events[] =
{
    { 0.2f, "PlaySound", "grunt/amb_idle_whimp" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaGruntIdleExtra3Events[] =
{
    { 0.1f, "PlaySound", "grunt/amb_idle_whimp" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaGruntNotice1Events[] =
{
    { 0.01f, "PlaySound", "grunt/notice" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaGruntNotice2Events[] =
{
    { 0.01f, "PlaySound", "grunt/notice_long" },
};

static const cLuxLlamaAnimation kLuxLlamaGruntAnimations[] =
{
    { "Idle", "servant_grunt/animations/idle.dae_anim", 1.0f, 0.0f, NULL, 0 },
    { "Run", "servant_grunt/animations/run.dae_anim", 0.2f, 0.0f, kLuxLlamaGruntRunEvents, sizeof(kLuxLlamaGruntRunEvents) / sizeof(kLuxLlamaGruntRunEvents[0]) },
    { "Walk", "servant_grunt/animations/walk.dae_anim", 1.0f, 0.0f, kLuxLlamaGruntWalkEvents, sizeof(kLuxLlamaGruntWalkEvents) / sizeof(kLuxLlamaGruntWalkEvents[0]) },
    { "SwingClaws01", "servant_grunt/animations/attack_short1.dae_anim", 1.1f, 0.5f, kLuxLlamaGruntSwingClaws01Events, sizeof(kLuxLlamaGruntSwingClaws01Events) / sizeof(kLuxLlamaGruntSwingClaws01Events[0]) },
    { "SwingClaws02", "servant_grunt/animations/attack_short2.dae_anim", 1.1f, 0.5f, kLuxLlamaGruntSwingClaws02Events, sizeof(kLuxLlamaGruntSwingClaws02Events) / sizeof(kLuxLlamaGruntSwingClaws02Events[0]) },
    { "SwingLaunch", "servant_grunt/animations/attack_run.dae_anim", 1.0f, 0.45f, kLuxLlamaGruntSwingLaunchEvents, sizeof(kLuxLlamaGruntSwingLaunchEvents) / sizeof(kLuxLlamaGruntSwingLaunchEvents[0]) },
    { "BreakDoor", "servant_grunt/animations/break_door.dae_anim", 1.0f, 0.5f, kLuxLlamaGruntBreakDoorEvents, sizeof(kLuxLlamaGruntBreakDoorEvents) / sizeof(kLuxLlamaGruntBreakDoorEvents[0]) },
    { "Flinch", "servant_grunt/animations/flinch.dae_anim", 0.75f, 0.0f, kLuxLlamaGruntFlinchEvents, sizeof(kLuxLlamaGruntFlinchEvents) / sizeof(kLuxLlamaGruntFlinchEvents[0]) },
    { "IdleExtra1", "servant_grunt/animations/idle_extra1.dae_anim", 1.0f, 0.0f, kLuxLlamaGruntIdleExtra1Events, sizeof(kLuxLlamaGruntIdleExtra1Events) / sizeof(kLuxLlamaGruntIdleExtra1Events[0]) },
    { "IdleExtra2", "servant_grunt/animations/idle_extra2.dae_anim", 1.0f, 0.0f, kLuxLlamaGruntIdleExtra2Events, sizeof(kLuxLlamaGruntIdleExtra2Events) / sizeof(kLuxLlamaGruntIdleExtra2Events[0]) },
    { "IdleExtra3", "servant_grunt/animations/idle_extra3.dae_anim", 1.0f, 0.0f, kLuxLlamaGruntIdleExtra3Events, sizeof(kLuxLlamaGruntIdleExtra3Events) / sizeof(kLuxLlamaGruntIdleExtra3Events[0]) },
    { "Notice1", "servant_grunt/animations/notice1.dae_anim", 1.0f, 0.0f, kLuxLlamaGruntNotice1Events, sizeof(kLuxLlamaGruntNotice1Events) / sizeof(kLuxLlamaGruntNotice1Events[0]) },
    { "Notice2", "servant_grunt/animations/notice2.dae_anim", 1.0f, 0.0f, kLuxLlamaGruntNotice2Events, sizeof(kLuxLlamaGruntNotice2Events) / sizeof(kLuxLlamaGruntNotice2Events[0]) },
};

static const cLuxLlamaAnimationEvent kLuxLlamaBruteRunEvents[] =
{
    { 0.2f, "PlaySound", "brute/metal_run" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaBruteWalkEvents[] =
{
    { 0.5f, "Step", "" },
    { 0.5f, "PlaySound", "brute/metal_walk" },
    { 1.5f, "Step", "" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaBruteSwingClaws01Events[] =
{
    { 0.1f, "PlaySound", "brute/metal_walk" },
    { 0.4f, "PlaySound", "brute/attack_claw" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaBruteSwingClaws02Events[] =
{
    { 0.2f, "PlaySound", "brute/metal_walk" },
    { 0.4f, "PlaySound", "brute/attack_claw" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaBruteSwingLaunchEvents[] =
{
    { 0.1f, "PlaySound", "brute/metal_run" },
    { 0.2f, "PlaySound", "brute/attack_launch" },
    { 0.3f, "Step", "" },
    { 1.0f, "Step", "" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaBruteBreakDoorEvents[] =
{
    { 0.2f, "PlaySound", "brute/metal_run" },
    { 0.5f, "PlaySound", "brute/attack_launch" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaBruteFlinchEvents[] =
{
    { 0.1f, "PlaySound", "brute/amb_alert" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaBruteIdleExtra1Events[] =
{
    { 0.4f, "PlaySound", "brute/amb_idle_scratch" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaBruteIdleExtra2Events[] =
{
    { 0.2f, "PlaySound", "brute/amb_idle_whimp" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaBruteNotice1Events[] =
{
    { 0.01f, "PlaySound", "brute/notice" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaBruteNotice2Events[] =
{
    { 0.01f, "PlaySound", "brute/notice_long" },
};

static const cLuxLlamaAnimation kLuxLlamaBruteAnimations[] =
{
    { "Idle", "servant_brute/animations/idle.dae_anim", 0.75f, 0.0f, NULL, 0 },
    { "Run", "servant_brute/animations/run.dae_anim", 0.2f, 0.0f, kLuxLlamaBruteRunEvents, sizeof(kLuxLlamaBruteRunEvents) / sizeof(kLuxLlamaBruteRunEvents[0]) },
    { "Walk", "servant_brute/animations/walk.dae_anim", 1.0f, 0.0f, kLuxLlamaBruteWalkEvents, sizeof(kLuxLlamaBruteWalkEvents) / sizeof(kLuxLlamaBruteWalkEvents[0]) },
    { "SwingClaws01", "servant_brute/animations/attack_short1.dae_anim", 1.1f, 0.65f, kLuxLlamaBruteSwingClaws01Events, sizeof(kLuxLlamaBruteSwingClaws01Events) / sizeof(kLuxLlamaBruteSwingClaws01Events[0]) },
    { "SwingClaws02", "servant_brute/animations/attack_short2.dae_anim", 1.1f, 0.5f, kLuxLlamaBruteSwingClaws02Events, sizeof(kLuxLlamaBruteSwingClaws02Events) / sizeof(kLuxLlamaBruteSwingClaws02Events[0]) },
    { "SwingLaunch", "servant_brute/animations/attack_run.dae_anim", 1.0f, 0.45f, kLuxLlamaBruteSwingLaunchEvents, sizeof(kLuxLlamaBruteSwingLaunchEvents) / sizeof(kLuxLlamaBruteSwingLaunchEvents[0]) },
    { "BreakDoor", "servant_brute/animations/break_door.dae_anim", 1.0f, 0.5f, kLuxLlamaBruteBreakDoorEvents, sizeof(kLuxLlamaBruteBreakDoorEvents) / sizeof(kLuxLlamaBruteBreakDoorEvents[0]) },
    { "Flinch", "servant_brute/animations/flinch.dae_anim", 1.0f, 0.0f, kLuxLlamaBruteFlinchEvents, sizeof(kLuxLlamaBruteFlinchEvents) / sizeof(kLuxLlamaBruteFlinchEvents[0]) },
    { "IdleExtra1", "servant_brute/animations/idle_extra1.dae_anim", 1.0f, 0.0f, kLuxLlamaBruteIdleExtra1Events, sizeof(kLuxLlamaBruteIdleExtra1Events) / sizeof(kLuxLlamaBruteIdleExtra1Events[0]) },
    { "IdleExtra2", "servant_brute/animations/idle_extra2.dae_anim", 1.0f, 0.0f, kLuxLlamaBruteIdleExtra2Events, sizeof(kLuxLlamaBruteIdleExtra2Events) / sizeof(kLuxLlamaBruteIdleExtra2Events[0]) },
    { "Notice1", "servant_brute/animations/notice1.dae_anim", 1.0f, 0.0f, kLuxLlamaBruteNotice1Events, sizeof(kLuxLlamaBruteNotice1Events) / sizeof(kLuxLlamaBruteNotice1Events[0]) },
    { "Notice2", "servant_brute/animations/notice2.dae_anim", 1.0f, 0.0f, kLuxLlamaBruteNotice2Events, sizeof(kLuxLlamaBruteNotice2Events) / sizeof(kLuxLlamaBruteNotice2Events[0]) },
};

static const cLuxLlamaAnimationEvent kLuxLlamaSuitorRunEvents[] =
{
    { 0.2f, "PlaySound", "suitor/metal_run" },
    { 0.02f, "PlaySound", "suitor/chains_monster_man_run_soft" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaSuitorWalkEvents[] =
{
    { 0.5f, "Step", "" },
    { 0.4f, "PlaySound", "suitor/chains_monster_man_walk_soft" },
    { 0.5f, "PlaySound", "suitor/metal_walk" },
    { 1.2f, "PlaySound", "suitor/chains_monster_man_walk_soft" },
    { 1.3f, "PlaySound", "suitor/metal_walk" },
    { 1.5f, "Step", "" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaSuitorSwingClaws01Events[] =
{
    { 0.1f, "PlaySound", "suitor/chains_monster_man_sneak_soft" },
    { 0.6f, "PlaySound", "suitor/attack_claw" },
    { 0.6f, "PlaySound", "suitor/chains_monster_man_run_soft" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaSuitorSwingClaws02Events[] =
{
    { 0.2f, "PlaySound", "suitor/chains_monster_man_sneak_soft" },
    { 0.4f, "PlaySound", "suitor/attack_claw" },
    { 0.4f, "PlaySound", "suitor/chains_monster_man_run_soft" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaSuitorSwingLaunchEvents[] =
{
    { 0.1f, "PlaySound", "suitor/chains_monster_man_sneak_soft" },
    { 0.2f, "PlaySound", "suitor/attack_launch" },
    { 0.2f, "PlaySound", "suitor/chains_monster_man_run_soft" },
    { 0.3f, "Step", "" },
    { 1.0f, "Step", "" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaSuitorBreakDoorEvents[] =
{
    { 0.2f, "PlaySound", "suitor/chains_monster_man_sneak_soft" },
    { 0.5f, "PlaySound", "suitor/attack_launch" },
    { 0.5f, "PlaySound", "suitor/chains_monster_man_run_soft" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaSuitorFlinchEvents[] =
{
    { 0.02f, "PlaySound", "suitor/chains_monster_man_sneak_soft" },
    { 0.1f, "PlaySound", "suitor/amb_alert" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaSuitorIdleExtra1Events[] =
{
    { 0.3f, "PlaySound", "suitor/chains_monster_man_sneak_soft" },
    { 0.4f, "PlaySound", "suitor/amb_idle_scratch" },
    { 1.0f, "PlaySound", "suitor/chains_monster_man_sneak_soft" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaSuitorIdleExtra2Events[] =
{
    { 0.1f, "PlaySound", "suitor/chains_monster_man_sneak_soft" },
    { 0.2f, "PlaySound", "suitor/amb_idle_whimp" },
    { 0.8f, "PlaySound", "suitor/chains_monster_man_sneak_soft" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaSuitorNotice1Events[] =
{
    { 0.1f, "PlaySound", "suitor/chains_monster_man_walk_soft" },
    { 0.01f, "PlaySound", "suitor/notice" },
};

static const cLuxLlamaAnimationEvent kLuxLlamaSuitorNotice2Events[] =
{
    { 0.1f, "PlaySound", "suitor/chains_monster_man_walk_soft" },
    { 0.01f, "PlaySound", "suitor/notice_long" },
    { 1.2f, "PlaySound", "suitor/chains_monster_man_sneak_soft" },
};

static const cLuxLlamaAnimation kLuxLlamaSuitorAnimations[] =
{
    { "Idle", "enemy_suitor/animations/idle.dae_anim", 0.75f, 0.0f, NULL, 0 },
    { "Run", "enemy_suitor/animations/run.dae_anim", 0.2f, 0.0f, kLuxLlamaSuitorRunEvents, sizeof(kLuxLlamaSuitorRunEvents) / sizeof(kLuxLlamaSuitorRunEvents[0]) },
    { "Walk", "enemy_suitor/animations/walk.dae_anim", 1.0f, 0.0f, kLuxLlamaSuitorWalkEvents, sizeof(kLuxLlamaSuitorWalkEvents) / sizeof(kLuxLlamaSuitorWalkEvents[0]) },
    { "SwingClaws01", "enemy_suitor/animations/attack_short1.dae_anim", 1.1f, 0.65f, kLuxLlamaSuitorSwingClaws01Events, sizeof(kLuxLlamaSuitorSwingClaws01Events) / sizeof(kLuxLlamaSuitorSwingClaws01Events[0]) },
    { "SwingClaws02", "enemy_suitor/animations/attack_short2.dae_anim", 1.1f, 0.5f, kLuxLlamaSuitorSwingClaws02Events, sizeof(kLuxLlamaSuitorSwingClaws02Events) / sizeof(kLuxLlamaSuitorSwingClaws02Events[0]) },
    { "SwingLaunch", "enemy_suitor/animations/attack_run.dae_anim", 1.0f, 0.45f, kLuxLlamaSuitorSwingLaunchEvents, sizeof(kLuxLlamaSuitorSwingLaunchEvents) / sizeof(kLuxLlamaSuitorSwingLaunchEvents[0]) },
    { "BreakDoor", "enemy_suitor/animations/break_door.dae_anim", 1.0f, 0.5f, kLuxLlamaSuitorBreakDoorEvents, sizeof(kLuxLlamaSuitorBreakDoorEvents) / sizeof(kLuxLlamaSuitorBreakDoorEvents[0]) },
    { "Flinch", "enemy_suitor/animations/flinch.dae_anim", 1.0f, 0.0f, kLuxLlamaSuitorFlinchEvents, sizeof(kLuxLlamaSuitorFlinchEvents) / sizeof(kLuxLlamaSuitorFlinchEvents[0]) },
    { "IdleExtra1", "enemy_suitor/animations/idle_extra1.dae_anim", 1.0f, 0.0f, kLuxLlamaSuitorIdleExtra1Events, sizeof(kLuxLlamaSuitorIdleExtra1Events) / sizeof(kLuxLlamaSuitorIdleExtra1Events[0]) },
    { "IdleExtra2", "enemy_suitor/animations/idle_extra2.dae_anim", 1.0f, 0.0f, kLuxLlamaSuitorIdleExtra2Events, sizeof(kLuxLlamaSuitorIdleExtra2Events) / sizeof(kLuxLlamaSuitorIdleExtra2Events[0]) },
    { "Notice1", "enemy_suitor/animations/notice1.dae_anim", 1.0f, 0.0f, kLuxLlamaSuitorNotice1Events, sizeof(kLuxLlamaSuitorNotice1Events) / sizeof(kLuxLlamaSuitorNotice1Events[0]) },
    { "Notice2", "enemy_suitor/animations/notice2.dae_anim", 1.0f, 0.0f, kLuxLlamaSuitorNotice2Events, sizeof(kLuxLlamaSuitorNotice2Events) / sizeof(kLuxLlamaSuitorNotice2Events[0]) },
};

static const cLuxLlamaAnimation kLuxLlamaManPigAnimations[] =
{
    { "IdleExtra1", "manpig/animations/manpig_breathing.fbx", 1.000000f, 0.000000f, NULL, 0 },
};

static const cLuxLlamaAnimationProfile kLuxLlamaAnimationProfiles[] =
{
    { "Grunt", kLuxLlamaGruntAnimations, sizeof(kLuxLlamaGruntAnimations) / sizeof(kLuxLlamaGruntAnimations[0]) },
    { "Brute", kLuxLlamaBruteAnimations, sizeof(kLuxLlamaBruteAnimations) / sizeof(kLuxLlamaBruteAnimations[0]) },
    { "Suitor", kLuxLlamaSuitorAnimations, sizeof(kLuxLlamaSuitorAnimations) / sizeof(kLuxLlamaSuitorAnimations[0]) },
    { "ManPig", kLuxLlamaManPigAnimations, sizeof(kLuxLlamaManPigAnimations) / sizeof(kLuxLlamaManPigAnimations[0]) },
};
static const std::size_t kLuxLlamaAnimationProfileCount =
    sizeof(kLuxLlamaAnimationProfiles) / sizeof(kLuxLlamaAnimationProfiles[0]);

#endif // LUX_ENEMY_LLAMA_ANIMATIONS_H
