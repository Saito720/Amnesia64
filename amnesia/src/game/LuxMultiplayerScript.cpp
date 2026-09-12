#include "LuxMultiplayerScript.h"
#include "LuxScriptHandler.h"
#include "LuxMultiplayerContent.h"

unsigned cLuxMultiplayerScriptScope::smDepth=0;

// Every asset site in the typed registry consumes one bit, in argument order.
// A missing host resource only relaxes that matching client lookup; other
// resources in the same command remain strict. Native commands still run so
// their non-resource side effects (credits, subtitles, etc.) are preserved.
struct cScriptResourceValidation {
    bool apply;
    std::string& error;
    uint32_t optionalMask=0, unavailableMask=0, checkedMask=0;
    unsigned nextResource=0;
    bool overflow=false;
    cScriptResourceValidation(bool execute,std::string& message):apply(execute),error(message) {}
    bool Asset(const std::string& name,const std::string& type,std::string& message) {
        if(nextResource>=32) {overflow=true;message="Too many script resource arguments.";return false;}
        const uint32_t bit=uint32_t(1)<<nextResource++;
        checkedMask|=bit;
        if(apply && (optionalMask&bit)) return true;
        if(LuxValidateMultiplayerAsset(name,type,message)) return true;
        if(apply) return false;
        unavailableMask|=bit;message.clear();return true;
    }
    bool Finish() {
        if(overflow || (optionalMask&~checkedMask)) {error="Invalid script resource mask.";return false;}
        return true;
    }
    template<class Callback> bool Invoke(const Callback& callback) {
        if(!Finish()) return false;
        if(apply) callback();
        return true;
    }
    bool Read(luxnet::Reader& r);
};
bool cScriptResourceValidation::Read(luxnet::Reader& r) {
    cScriptResourceValidation& resources=*this;
    error.clear();
    uint32_t id=r.U32();
    if(id & LuxScriptOptionalResources) {
        resources.optionalMask=r.U32();id &= ~LuxScriptOptionalResources;
        if(!r.valid || !resources.optionalMask) return false;
    }
    switch(id) {
    case 1: { // StartCredits
        std::string asMusic = r.String(4096);
        bool abLoopMusic = r.U8()!=0;
        std::string asTextCat = r.String(4096);
        std::string asTextEntry = r.String(4096);
        int alEndNum = static_cast<int32_t>(r.U32());
        if(!r.Done()) return false;
        if(!resources.Asset(asMusic,"audio",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StartCredits(asMusic, abLoopMusic, asTextCat, asTextEntry, alEndNum); });
    }
    case 2: { // StartDemoEnd
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StartDemoEnd(); });
    }
    case 3: { // SetMapDisplayNameEntry
        std::string asNameEntry = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetMapDisplayNameEntry(asNameEntry); });
    }
    case 4: { // SetSkyBoxActive
        bool abActive = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetSkyBoxActive(abActive); });
    }
    case 5: { // SetSkyBoxTexture
        std::string asTexture = r.String(4096);
        if(!r.Done()) return false;
        if(!resources.Asset(asTexture,"cubemap",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetSkyBoxTexture(asTexture); });
    }
    case 6: { // SetSkyBoxColor
        float afR = r.Float();
        float afG = r.Float();
        float afB = r.Float();
        float afA = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetSkyBoxColor(afR, afG, afB, afA); });
    }
    case 7: { // SetFogActive
        bool abActive = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetFogActive(abActive); });
    }
    case 8: { // SetFogColor
        float afR = r.Float();
        float afG = r.Float();
        float afB = r.Float();
        float afA = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetFogColor(afR, afG, afB, afA); });
    }
    case 9: { // SetFogProperties
        float afStart = r.Float();
        float afEnd = r.Float();
        float afFalloffExp = r.Float();
        bool abCulling = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetFogProperties(afStart, afEnd, afFalloffExp, abCulling); });
    }
    case 10: { // SetupLoadScreen
        std::string asTextCat = r.String(4096);
        std::string asTextEntry = r.String(4096);
        int alRandomNum = static_cast<int32_t>(r.U32());
        std::string asImageFile = r.String(4096);
        if(!r.Done()) return false;
        if(!resources.Asset(asImageFile,"texture",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetupLoadScreen(asTextCat, asTextEntry, alRandomNum, asImageFile); });
    }
    case 11: { // FadeIn
        float afTime = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadeIn(afTime); });
    }
    case 12: { // FadeOut
        float afTime = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadeOut(afTime); });
    }
    case 13: { // FadeImageTrailTo
        float afAmount = r.Float();
        float afSpeed = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadeImageTrailTo(afAmount, afSpeed); });
    }
    case 14: { // FadeSepiaColorTo
        float afAmount = r.Float();
        float afSpeed = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadeSepiaColorTo(afAmount, afSpeed); });
    }
    case 15: { // FadeRadialBlurTo
        float afSize = r.Float();
        float afSpeed = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadeRadialBlurTo(afSize, afSpeed); });
    }
    case 16: { // SetRadialBlurStartDist
        float afStartDist = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetRadialBlurStartDist(afStartDist); });
    }
    case 17: { // StartEffectFlash
        float afFadeIn = r.Float();
        float afWhite = r.Float();
        float afFadeOut = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StartEffectFlash(afFadeIn, afWhite, afFadeOut); });
    }
    case 18: { // StartEffectEmotionFlash
        std::string asTextCat = r.String(4096);
        std::string asTextEntry = r.String(4096);
        std::string asSound = r.String(4096);
        if(!r.Done()) return false;
        if(!resources.Asset(asSound,"snt",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StartEffectEmotionFlash(asTextCat, asTextEntry, asSound); });
    }
    case 19: { // SetInDarknessEffectsActive
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetInDarknessEffectsActive(abX); });
    }
    case 20: { // AddEffectVoice
        std::string asVoiceFile = r.String(4096);
        std::string asEffectFile = r.String(4096);
        std::string asTextCat = r.String(4096);
        std::string asTextEntry = r.String(4096);
        bool abUsePostion = r.U8()!=0;
        std::string asPosEntity = r.String(4096);
        float afMinDistance = r.Float();
        float afMaxDistance = r.Float();
        if(!r.Done()) return false;
        if(!resources.Asset(asVoiceFile,"audio",error)) return false;
        if(!resources.Asset(asEffectFile,"audio",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddEffectVoice(asVoiceFile, asEffectFile, asTextCat, asTextEntry, abUsePostion, asPosEntity, afMinDistance, afMaxDistance); });
    }
    case 21: { // StopAllEffectVoices
        float afFadeOutTime = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StopAllEffectVoices(afFadeOutTime); });
    }
    case 22: { // StartPlayerSpawnPS
        std::string asSPSFile = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StartPlayerSpawnPS(asSPSFile); });
    }
    case 23: { // StopPlayerSpawnPS
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StopPlayerSpawnPS(); });
    }
    case 24: { // StartScreenShake
        float afAmount = r.Float();
        float afTime = r.Float();
        float afFadeInTime = r.Float();
        float afFadeOutTime = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StartScreenShake(afAmount, afTime, afFadeInTime, afFadeOutTime); });
    }
    case 25: { // SetInsanitySetEnabled
        std::string asSet = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetInsanitySetEnabled(asSet, abX); });
    }
    case 26: { // StartRandomInsanityEvent
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StartRandomInsanityEvent(); });
    }
    case 27: { // StartInsanityEvent
        std::string asEventName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StartInsanityEvent(asEventName); });
    }
    case 28: { // StopCurrentInsanityEvent
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StopCurrentInsanityEvent(); });
    }
    case 29: { // PlayGuiSound
        std::string asSoundEntFile = r.String(4096);
        float afVolume = r.Float();
        if(!r.Done()) return false;
        if(!resources.Asset(asSoundEntFile,"gui_sound",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::PlayGuiSound(asSoundEntFile, afVolume); });
    }
    case 30: { // SetPlayerActive
        bool abActive = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerActive(abActive); });
    }
    case 31: { // ChangePlayerStateToNormal
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::ChangePlayerStateToNormal(); });
    }
    case 32: { // SetPlayerCrouching
        bool abCrouch = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerCrouching(abCrouch); });
    }
    case 33: { // AddPlayerBodyForce
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        bool abUseLocalCoords = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddPlayerBodyForce(afX, afY, afZ, abUseLocalCoords); });
    }
    case 34: { // ShowPlayerCrossHairIcons
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::ShowPlayerCrossHairIcons(abX); });
    }
    case 35: { // SetPlayerPos
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerPos(afX, afY, afZ); });
    }
    case 36: { // SetPlayerSanity
        float afSanity = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerSanity(afSanity); });
    }
    case 37: { // AddPlayerSanity
        float afSanity = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddPlayerSanity(afSanity); });
    }
    case 38: { // SetPlayerHealth
        float afHealth = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerHealth(afHealth); });
    }
    case 39: { // AddPlayerHealth
        float afHealth = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddPlayerHealth(afHealth); });
    }
    case 40: { // SetPlayerLampOil
        float afOil = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerLampOil(afOil); });
    }
    case 41: { // AddPlayerLampOil
        float afOil = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddPlayerLampOil(afOil); });
    }
    case 42: { // MovePlayerForward
        float afAmount = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::MovePlayerForward(afAmount); });
    }
    case 43: { // SetPlayerPermaDeathSound
        std::string asSound = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerPermaDeathSound(asSound); });
    }
    case 44: { // SetSanityDrainDisabled
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetSanityDrainDisabled(abX); });
    }
    case 45: { // GiveSanityBoost
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::GiveSanityBoost(); });
    }
    case 46: { // GiveSanityBoostSmall
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::GiveSanityBoostSmall(); });
    }
    case 47: { // GivePlayerDamage
        float afAmount = r.Float();
        std::string asType = r.String(4096);
        bool abSpinHead = r.U8()!=0;
        bool abLethal = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::GivePlayerDamage(afAmount, asType, abSpinHead, abLethal); });
    }
    case 48: { // FadePlayerFOVMulTo
        float afX = r.Float();
        float afSpeed = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadePlayerFOVMulTo(afX, afSpeed); });
    }
    case 49: { // FadePlayerAspectMulTo
        float afX = r.Float();
        float afSpeed = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadePlayerAspectMulTo(afX, afSpeed); });
    }
    case 50: { // FadePlayerRollTo
        float afX = r.Float();
        float afSpeedMul = r.Float();
        float afMaxSpeed = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadePlayerRollTo(afX, afSpeedMul, afMaxSpeed); });
    }
    case 51: { // MovePlayerHeadPos
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        float afSpeed = r.Float();
        float afSlowDownDist = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::MovePlayerHeadPos(afX, afY, afZ, afSpeed, afSlowDownDist); });
    }
    case 52: { // StartPlayerLookAt
        std::string asEntityName = r.String(4096);
        float afSpeedMul = r.Float();
        float afMaxSpeed = r.Float();
        std::string asAtTargetCallback = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StartPlayerLookAt(asEntityName, afSpeedMul, afMaxSpeed, asAtTargetCallback); });
    }
    case 53: { // StopPlayerLookAt
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StopPlayerLookAt(); });
    }
    case 54: { // SetPlayerMoveSpeedMul
        float afMul = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerMoveSpeedMul(afMul); });
    }
    case 55: { // SetPlayerRunSpeedMul
        float afMul = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerRunSpeedMul(afMul); });
    }
    case 56: { // SetPlayerLookSpeedMul
        float afMul = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerLookSpeedMul(afMul); });
    }
    case 57: { // SetPlayerJumpForceMul
        float afMul = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerJumpForceMul(afMul); });
    }
    case 58: { // SetPlayerJumpDisabled
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerJumpDisabled(abX); });
    }
    case 59: { // SetPlayerCrouchDisabled
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerCrouchDisabled(abX); });
    }
    case 60: { // SetPlayerFallDamageDisabled
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPlayerFallDamageDisabled(abX); });
    }
    case 61: { // TeleportPlayer
        std::string asStartPosName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::TeleportPlayer(asStartPosName); });
    }
    case 62: { // SetLanternActive
        bool abX = r.U8()!=0;
        bool abUseEffects = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetLanternActive(abX, abUseEffects); });
    }
    case 63: { // SetLanternDisabled
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetLanternDisabled(abX); });
    }
    case 64: { // SetMessage
        std::string asTextCategory = r.String(4096);
        std::string asTextEntry = r.String(4096);
        float afTime = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetMessage(asTextCategory, asTextEntry, afTime); });
    }
    case 65: { // SetDeathHint
        std::string asTextCategory = r.String(4096);
        std::string asTextEntry = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetDeathHint(asTextCategory, asTextEntry); });
    }
    case 66: { // DisableDeathStartSound
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::DisableDeathStartSound(); });
    }
    case 67: { // AddNote
        std::string asNameAndTextEntry = r.String(4096);
        std::string asImage = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddNote(asNameAndTextEntry, asImage); });
    }
    case 68: { // AddDiary
        std::string asNameAndTextEntry = r.String(4096);
        std::string asImage = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddDiary(asNameAndTextEntry, asImage); });
    }
    case 69: { // ReturnOpenJournal
        bool abOpenJournal = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::ReturnOpenJournal(abOpenJournal); });
    }
    case 70: { // AddQuest
        std::string asName = r.String(4096);
        std::string asNameAndTextEntry = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddQuest(asName, asNameAndTextEntry); });
    }
    case 71: { // CompleteQuest
        std::string asName = r.String(4096);
        std::string asNameAndTextEntry = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::CompleteQuest(asName, asNameAndTextEntry); });
    }
    case 72: { // SetNumberOfQuestsInMap
        int alNumberOfQuests = static_cast<int32_t>(r.U32());
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetNumberOfQuestsInMap(alNumberOfQuests); });
    }
    case 73: { // GiveHint
        std::string asName = r.String(4096);
        std::string asMessageCat = r.String(4096);
        std::string asMessageEntry = r.String(4096);
        float afTimeShown = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::GiveHint(asName, asMessageCat, asMessageEntry, afTimeShown); });
    }
    case 74: { // RemoveHint
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::RemoveHint(asName); });
    }
    case 75: { // BlockHint
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::BlockHint(asName); });
    }
    case 76: { // UnBlockHint
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::UnBlockHint(asName); });
    }
    case 77: { // ExitInventory
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::ExitInventory(); });
    }
    case 78: { // SetInventoryDisabled
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetInventoryDisabled(abX); });
    }
    case 79: { // SetInventoryMessage
        std::string asTextCategory = r.String(4096);
        std::string asTextEntry = r.String(4096);
        float afTime = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetInventoryMessage(asTextCategory, asTextEntry, afTime); });
    }
    case 80: { // GiveItem
        std::string asName = r.String(4096);
        std::string asType = r.String(4096);
        std::string asSubTypeName = r.String(4096);
        std::string asImageName = r.String(4096);
        float afAmount = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::GiveItem(asName, asType, asSubTypeName, asImageName, afAmount); });
    }
    case 81: { // GiveItemFromFile
        std::string asName = r.String(4096);
        std::string asFileName = r.String(4096);
        if(!r.Done()) return false;
        if(!resources.Asset(asFileName,"ent",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::GiveItemFromFile(asName, asFileName); });
    }
    case 82: { // RemoveItem
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::RemoveItem(asName); });
    }
    case 83: { // PreloadParticleSystem
        std::string asPSFile = r.String(4096);
        if(!r.Done()) return false;
        if(!resources.Asset(asPSFile,"ps",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::PreloadParticleSystem(asPSFile); });
    }
    case 84: { // PreloadSound
        std::string asSoundFile = r.String(4096);
        if(!r.Done()) return false;
        // PreloadSound is an optional cache warm-up, not a requirement to play
        // this sound. Retail scripts contain raw samples, obsolete names and
        // typos here; the native preloader only warns when it cannot load them.
        // Keep the validation before touching XML, but skip unusable hints
        // without failing the session. Actual playback is still validated.
        if(!resources.Asset(asSoundFile,"snt",error)) {
            if(apply) hpl::Warning("Skipping optional multiplayer sound preload '%s': %s\n",asSoundFile.c_str(),error.c_str());
            error.clear();return resources.Finish();
        }
        return resources.Invoke([&] { cLuxScriptHandler::PreloadSound(asSoundFile); });
    }
    case 85: { // CreateParticleSystemAtEntity
        std::string asPSName = r.String(4096);
        std::string asPSFile = r.String(4096);
        std::string asEntity = r.String(4096);
        bool abSavePS = r.U8()!=0;
        if(!r.Done()) return false;
        if(!resources.Asset(asPSFile,"ps",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::CreateParticleSystemAtEntity(asPSName, asPSFile, asEntity, abSavePS); });
    }
    case 86: { // CreateParticleSystemAtEntityExt
        std::string asPSName = r.String(4096);
        std::string asPSFile = r.String(4096);
        std::string asEntity = r.String(4096);
        bool abSavePS = r.U8()!=0;
        float afR = r.Float();
        float afG = r.Float();
        float afB = r.Float();
        float afA = r.Float();
        bool abFadeAtDistance = r.U8()!=0;
        float afFadeMinEnd = r.Float();
        float afFadeMinStart = r.Float();
        float afFadeMaxStart = r.Float();
        float afFadeMaxEnd = r.Float();
        if(!r.Done()) return false;
        if(!resources.Asset(asPSFile,"ps",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::CreateParticleSystemAtEntityExt(asPSName, asPSFile, asEntity, abSavePS, afR, afG, afB, afA, abFadeAtDistance, afFadeMinEnd, afFadeMinStart, afFadeMaxStart, afFadeMaxEnd); });
    }
    case 87: { // DestroyParticleSystem
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::DestroyParticleSystem(asName); });
    }
    case 88: { // PlaySoundAtEntity
        std::string asSoundName = r.String(4096);
        std::string asSoundFile = r.String(4096);
        std::string asEntity = r.String(4096);
        float afFadeTime = r.Float();
        bool abSaveSound = r.U8()!=0;
        if(!r.Done()) return false;
        if(!resources.Asset(asSoundFile,"snt",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::PlaySoundAtEntity(asSoundName, asSoundFile, asEntity, afFadeTime, abSaveSound); });
    }
    case 89: { // FadeInSound
        std::string asSoundName = r.String(4096);
        float afFadeTime = r.Float();
        bool abPlayStart = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadeInSound(asSoundName, afFadeTime, abPlayStart); });
    }
    case 90: { // StopSound
        std::string asSoundName = r.String(4096);
        float afFadeTime = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StopSound(asSoundName, afFadeTime); });
    }
    case 91: { // SetLightVisible
        std::string asLightName = r.String(4096);
        bool abVisible = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetLightVisible(asLightName, abVisible); });
    }
    case 92: { // FadeLightTo
        std::string asLightName = r.String(4096);
        float afR = r.Float();
        float afG = r.Float();
        float afB = r.Float();
        float afA = r.Float();
        float afRadius = r.Float();
        float afTime = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadeLightTo(asLightName, afR, afG, afB, afA, afRadius, afTime); });
    }
    case 93: { // SetLightFlickerActive
        std::string asLightName = r.String(4096);
        bool abActive = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetLightFlickerActive(asLightName, abActive); });
    }
    case 94: { // PlayMusic
        std::string asMusicFile = r.String(4096);
        bool abLoop = r.U8()!=0;
        float afVolume = r.Float();
        float afFadeTime = r.Float();
        int alPrio = static_cast<int32_t>(r.U32());
        bool abResume = r.U8()!=0;
        if(!r.Done()) return false;
        if(!resources.Asset(asMusicFile,"audio",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::PlayMusic(asMusicFile, abLoop, afVolume, afFadeTime, alPrio, abResume); });
    }
    case 95: { // StopMusic
        float afFadeTime = r.Float();
        int alPrio = static_cast<int32_t>(r.U32());
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StopMusic(afFadeTime, alPrio); });
    }
    case 96: { // FadeGlobalSoundVolume
        float afDestVolume = r.Float();
        float afTime = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadeGlobalSoundVolume(afDestVolume, afTime); });
    }
    case 97: { // FadeGlobalSoundSpeed
        float afDestSpeed = r.Float();
        float afTime = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadeGlobalSoundSpeed(afDestSpeed, afTime); });
    }
    case 98: { // SetEntityActive
        std::string asName = r.String(4096);
        bool abActive = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetEntityActive(asName, abActive); });
    }
    case 99: { // SetEntityVisible
        std::string asName = r.String(4096);
        bool abVisible = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetEntityVisible(asName, abVisible); });
    }
    case 100: { // SetEntityPos
        std::string asName = r.String(4096);
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetEntityPos(asName, afX, afY, afZ); });
    }
    case 101: { // SetEntityCustomFocusCrossHair
        std::string asName = r.String(4096);
        std::string asCrossHair = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetEntityCustomFocusCrossHair(asName, asCrossHair); });
    }
    case 102: { // CreateEntityAtArea
        std::string asEntityName = r.String(4096);
        std::string asEntityFile = r.String(4096);
        std::string asAreaName = r.String(4096);
        bool abFullGameSave = r.U8()!=0;
        if(!r.Done()) return false;
        if(!resources.Asset(asEntityFile,"ent",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::CreateEntityAtArea(asEntityName, asEntityFile, asAreaName, abFullGameSave); });
    }
    case 103: { // ReplaceEntity
        std::string asName = r.String(4096);
        std::string asBodyName = r.String(4096);
        std::string asNewEntityName = r.String(4096);
        std::string asNewEntityFile = r.String(4096);
        bool abFullGameSave = r.U8()!=0;
        if(!r.Done()) return false;
        if(!resources.Asset(asNewEntityFile,"ent",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::ReplaceEntity(asName, asBodyName, asNewEntityName, asNewEntityFile, abFullGameSave); });
    }
    case 104: { // PlaceEntityAtEntity
        std::string asName = r.String(4096);
        std::string asTargetEntity = r.String(4096);
        std::string asTargetBodyName = r.String(4096);
        bool abUseRotation = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::PlaceEntityAtEntity(asName, asTargetEntity, asTargetBodyName, abUseRotation); });
    }
    case 105: { // SetEntityInteractionDisabled
        std::string asName = r.String(4096);
        bool abDisabled = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetEntityInteractionDisabled(asName, abDisabled); });
    }
    case 106: { // SetPropEffectActive
        std::string asName = r.String(4096);
        bool abActive = r.U8()!=0;
        bool abFadeAndPlaySounds = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPropEffectActive(asName, abActive, abFadeAndPlaySounds); });
    }
    case 107: { // SetPropActiveAndFade
        std::string asName = r.String(4096);
        bool abActive = r.U8()!=0;
        float afFadeTime = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPropActiveAndFade(asName, abActive, afFadeTime); });
    }
    case 108: { // SetPropStaticPhysics
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPropStaticPhysics(asName, abX); });
    }
    case 109: { // RotatePropToSpeed
        std::string asName = r.String(4096);
        float afAcc = r.Float();
        float afGoalSpeed = r.Float();
        float afAxisX = r.Float();
        float afAxisY = r.Float();
        float afAxisZ = r.Float();
        bool abResetSpeed = r.U8()!=0;
        std::string asOffsetArea = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::RotatePropToSpeed(asName, afAcc, afGoalSpeed, afAxisX, afAxisY, afAxisZ, abResetSpeed, asOffsetArea); });
    }
    case 110: { // AttachPropToProp
        std::string asPropName = r.String(4096);
        std::string asAttachName = r.String(4096);
        std::string asAttachFile = r.String(4096);
        float afPosX = r.Float();
        float afPosY = r.Float();
        float afPosZ = r.Float();
        float afRotX = r.Float();
        float afRotY = r.Float();
        float afRotZ = r.Float();
        if(!r.Done()) return false;
        if(!resources.Asset(asAttachFile,"ent",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AttachPropToProp(asPropName, asAttachName, asAttachFile, afPosX, afPosY, afPosZ, afRotX, afRotY, afRotZ); });
    }
    case 111: { // AddAttachedPropToProp
        std::string asPropName = r.String(4096);
        std::string asAttachName = r.String(4096);
        std::string asAttachFile = r.String(4096);
        float afPosX = r.Float();
        float afPosY = r.Float();
        float afPosZ = r.Float();
        float afRotX = r.Float();
        float afRotY = r.Float();
        float afRotZ = r.Float();
        if(!r.Done()) return false;
        if(!resources.Asset(asAttachFile,"ent",error)) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddAttachedPropToProp(asPropName, asAttachName, asAttachFile, afPosX, afPosY, afPosZ, afRotX, afRotY, afRotZ); });
    }
    case 112: { // RemoveAttachedPropFromProp
        std::string asPropName = r.String(4096);
        std::string asAttachName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::RemoveAttachedPropFromProp(asPropName, asAttachName); });
    }
    case 113: { // SetLampLit
        std::string asName = r.String(4096);
        bool abLit = r.U8()!=0;
        bool abEffects = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetLampLit(asName, abLit, abEffects); });
    }
    case 114: { // SetSwingDoorLocked
        std::string asName = r.String(4096);
        bool abLocked = r.U8()!=0;
        bool abEffects = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetSwingDoorLocked(asName, abLocked, abEffects); });
    }
    case 115: { // SetSwingDoorClosed
        std::string asName = r.String(4096);
        bool abClosed = r.U8()!=0;
        bool abEffects = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetSwingDoorClosed(asName, abClosed, abEffects); });
    }
    case 116: { // SetSwingDoorDisableAutoClose
        std::string asName = r.String(4096);
        bool abDisableAutoClose = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetSwingDoorDisableAutoClose(asName, abDisableAutoClose); });
    }
    case 117: { // SetLevelDoorLocked
        std::string asName = r.String(4096);
        bool abLocked = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetLevelDoorLocked(asName, abLocked); });
    }
    case 118: { // SetLevelDoorLockedSound
        std::string asName = r.String(4096);
        std::string asSound = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetLevelDoorLockedSound(asName, asSound); });
    }
    case 119: { // SetLevelDoorLockedText
        std::string asName = r.String(4096);
        std::string asTextCat = r.String(4096);
        std::string asTextEntry = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetLevelDoorLockedText(asName, asTextCat, asTextEntry); });
    }
    case 120: { // SetPropObjectStuckState
        std::string asName = r.String(4096);
        int alState = static_cast<int32_t>(r.U32());
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPropObjectStuckState(asName, alState); });
    }
    case 121: { // SetWheelAngle
        std::string asName = r.String(4096);
        float afAngle = r.Float();
        bool abAutoMove = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetWheelAngle(asName, afAngle, abAutoMove); });
    }
    case 122: { // SetWheelStuckState
        std::string asName = r.String(4096);
        int alState = static_cast<int32_t>(r.U32());
        bool afEffects = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetWheelStuckState(asName, alState, afEffects); });
    }
    case 123: { // SetLeverStuckState
        std::string asName = r.String(4096);
        int alState = static_cast<int32_t>(r.U32());
        bool afEffects = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetLeverStuckState(asName, alState, afEffects); });
    }
    case 124: { // SetWheelInteractionDisablesStuck
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetWheelInteractionDisablesStuck(asName, abX); });
    }
    case 125: { // SetLeverInteractionDisablesStuck
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetLeverInteractionDisablesStuck(asName, abX); });
    }
    case 126: { // SetMultiSliderStuckState
        std::string asName = r.String(4096);
        int alStuckState = static_cast<int32_t>(r.U32());
        bool abEffects = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetMultiSliderStuckState(asName, alStuckState, abEffects); });
    }
    case 127: { // SetButtonSwitchedOn
        std::string asName = r.String(4096);
        bool abSwitchedOn = r.U8()!=0;
        bool abEffects = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetButtonSwitchedOn(asName, abSwitchedOn, abEffects); });
    }
    case 128: { // SetAllowStickyAreaAttachment
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetAllowStickyAreaAttachment(abX); });
    }
    case 129: { // AttachPropToStickyArea
        std::string asAreaName = r.String(4096);
        std::string asProp = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AttachPropToStickyArea(asAreaName, asProp); });
    }
    case 130: { // AttachBodyToStickyArea
        std::string asAreaName = r.String(4096);
        std::string asBody = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AttachBodyToStickyArea(asAreaName, asBody); });
    }
    case 131: { // DetachFromStickyArea
        std::string asAreaName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::DetachFromStickyArea(asAreaName); });
    }
    case 132: { // SetNPCAwake
        std::string asName = r.String(4096);
        bool abAwake = r.U8()!=0;
        bool abEffects = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetNPCAwake(asName, abAwake, abEffects); });
    }
    case 133: { // SetNPCFollowPlayer
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetNPCFollowPlayer(asName, abX); });
    }
    case 134: { // SetEnemyDisabled
        std::string asName = r.String(4096);
        bool abDisabled = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetEnemyDisabled(asName, abDisabled); });
    }
    case 135: { // SetEnemyIsHallucination
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetEnemyIsHallucination(asName, abX); });
    }
    case 136: { // FadeEnemyToSmoke
        std::string asName = r.String(4096);
        bool abPlaySound = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::FadeEnemyToSmoke(asName, abPlaySound); });
    }
    case 137: { // ShowEnemyPlayerPosition
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::ShowEnemyPlayerPosition(asName); });
    }
    case 138: { // AlertEnemyOfPlayerPresence
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AlertEnemyOfPlayerPresence(asName); });
    }
    case 139: { // SetEnemyDisableTriggers
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetEnemyDisableTriggers(asName, abX); });
    }
    case 140: { // AddEnemyPatrolNode
        std::string asName = r.String(4096);
        std::string asNodeName = r.String(4096);
        float afWaitTime = r.Float();
        std::string asAnimation = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddEnemyPatrolNode(asName, asNodeName, afWaitTime, asAnimation); });
    }
    case 141: { // ClearEnemyPatrolNodes
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::ClearEnemyPatrolNodes(asName); });
    }
    case 142: { // SetEnemySanityDecreaseActive
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetEnemySanityDecreaseActive(asName, abX); });
    }
    case 143: { // TeleportEnemyToNode
        std::string asName = r.String(4096);
        std::string asNodeName = r.String(4096);
        bool abChangeY = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::TeleportEnemyToNode(asName, asNodeName, abChangeY); });
    }
    case 144: { // TeleportEnemyToEntity
        std::string asName = r.String(4096);
        std::string asTargetEntity = r.String(4096);
        std::string asTargetBody = r.String(4096);
        bool abChangeY = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::TeleportEnemyToEntity(asName, asTargetEntity, asTargetBody, abChangeY); });
    }
    case 145: { // ChangeManPigPose
        std::string asName = r.String(4096);
        std::string asPoseType = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::ChangeManPigPose(asName, asPoseType); });
    }
    case 146: { // SetTeslaPigFadeDisabled
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetTeslaPigFadeDisabled(asName, abX); });
    }
    case 147: { // SetTeslaPigSoundDisabled
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetTeslaPigSoundDisabled(asName, abX); });
    }
    case 148: { // SetTeslaPigEasyEscapeDisabled
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetTeslaPigEasyEscapeDisabled(asName, abX); });
    }
    case 149: { // ForceTeslaPigSighting
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::ForceTeslaPigSighting(asName); });
    }
    case 150: { // SetMoveObjectState
        std::string asName = r.String(4096);
        float afState = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetMoveObjectState(asName, afState); });
    }
    case 151: { // SetMoveObjectStateExt
        std::string asName = r.String(4096);
        float afState = r.Float();
        float afAcc = r.Float();
        float afMaxSpeed = r.Float();
        float afSlowdownDist = r.Float();
        bool abResetSpeed = r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetMoveObjectStateExt(asName, afState, afAcc, afMaxSpeed, afSlowdownDist, abResetSpeed); });
    }
    case 152: { // SetPropHealth
        std::string asName = r.String(4096);
        float afHealth = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetPropHealth(asName, afHealth); });
    }
    case 153: { // AddPropHealth
        std::string asName = r.String(4096);
        float afHealth = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddPropHealth(asName, afHealth); });
    }
    case 154: { // ResetProp
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::ResetProp(asName); });
    }
    case 155: { // PlayPropAnimation
        std::string asProp = r.String(4096);
        std::string asAnimation = r.String(4096);
        float afFadeTime = r.Float();
        bool abLoop = r.U8()!=0;
        std::string asCallback = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::PlayPropAnimation(asProp, asAnimation, afFadeTime, abLoop, asCallback); });
    }
    case 156: { // AddPropForce
        std::string asName = r.String(4096);
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        std::string asCoordSystem = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddPropForce(asName, afX, afY, afZ, asCoordSystem); });
    }
    case 157: { // AddPropImpulse
        std::string asName = r.String(4096);
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        std::string asCoordSystem = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddPropImpulse(asName, afX, afY, afZ, asCoordSystem); });
    }
    case 158: { // AddBodyForce
        std::string asName = r.String(4096);
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        std::string asCoordSystem = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddBodyForce(asName, afX, afY, afZ, asCoordSystem); });
    }
    case 159: { // AddBodyImpulse
        std::string asName = r.String(4096);
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        std::string asCoordSystem = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::AddBodyImpulse(asName, afX, afY, afZ, asCoordSystem); });
    }
    case 160: { // BreakJoint
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::BreakJoint(asName); });
    }
    case 161: { // SetBodyMass
        std::string asName = r.String(4096);
        float afMass = r.Float();
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetBodyMass(asName, afMass); });
    }
    case 162: { // InteractConnectPropWithRope
        std::string asName = r.String(4096);
        std::string asPropName = r.String(4096);
        std::string asRopeName = r.String(4096);
        bool abInteractOnly = r.U8()!=0;
        float afSpeedMul = r.Float();
        float afMinSpeed = r.Float();
        float afMaxSpeed = r.Float();
        bool abInvert = r.U8()!=0;
        int alStatesUsed = static_cast<int32_t>(r.U32());
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::InteractConnectPropWithRope(asName, asPropName, asRopeName, abInteractOnly, afSpeedMul, afMinSpeed, afMaxSpeed, abInvert, alStatesUsed); });
    }
    case 163: { // InteractConnectPropWithMoveObject
        std::string asName = r.String(4096);
        std::string asPropName = r.String(4096);
        std::string asMoveObjectName = r.String(4096);
        bool abInteractOnly = r.U8()!=0;
        bool abInvert = r.U8()!=0;
        int alStatesUsed = static_cast<int32_t>(r.U32());
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::InteractConnectPropWithMoveObject(asName, asPropName, asMoveObjectName, abInteractOnly, abInvert, alStatesUsed); });
    }
    case 164: { // ConnectEntities
        std::string asName = r.String(4096);
        std::string asMainEntity = r.String(4096);
        std::string asConnectEntity = r.String(4096);
        bool abInvertStateSent = r.U8()!=0;
        int alStatesUsed = static_cast<int32_t>(r.U32());
        std::string asCallbackFunc = r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::ConnectEntities(asName, asMainEntity, asConnectEntity, abInvertStateSent, alStatesUsed, asCallbackFunc); });
    }
    case 165: { // SetEntityPlayerInteractCallback: advertise interactability, host executes callback.
        std::string name=r.String(4096),callback=r.String(4096);bool remove=r.U8()!=0;
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::SetEntityPlayerInteractCallback(name,callback,remove); });
    }
    case 166: { // StopPropMovement: collision-limited bridge/ladder motion stops on every peer.
        std::string name=r.String(4096);
        if(!r.Done()) return false;
        return resources.Invoke([&] { cLuxScriptHandler::StopPropMovement(name); });
    }
    case 167: { // CheckPoint: clients retain the host's local respawn position and death hint.
        std::string name=r.String(4096),start=r.String(4096),callback=r.String(4096);
        std::string hintCategory=r.String(4096),hintEntry=r.String(4096);
        if(!r.Done()) return false;
        // Client map scripts never run; retain only local respawn metadata.
        callback.clear();
        return resources.Invoke([&] { cLuxScriptHandler::CheckPoint(name,start,callback,hintCategory,hintEntry); });
    }
    default: return false;
    }
}

bool LuxValidateMultiplayerScriptEffect(luxnet::Reader& r, std::string& error,uint32_t* unavailableResources) {
    cScriptResourceValidation resources(false,error);
    const bool valid=resources.Read(r);
    if(unavailableResources) *unavailableResources=resources.unavailableMask;
    return valid;
}
bool LuxApplyMultiplayerScriptEffect(luxnet::Reader& r, std::string& error) {
    cScriptResourceValidation resources(true,error);
    return resources.Read(r);
}
