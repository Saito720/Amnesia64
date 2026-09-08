#include "LuxMultiplayerScript.h"
#include "LuxScriptHandler.h"
#include "LuxMultiplayerContent.h"

unsigned cLuxMultiplayerScriptScope::smDepth=0;

bool LuxApplyMultiplayerScriptEffect(luxnet::Reader& r, std::string& error) {
    uint32_t id=r.U32();
    switch(id) {
    case 1: { // StartCredits
        std::string asMusic = r.String(4096);
        bool abLoopMusic = r.U8()!=0;
        std::string asTextCat = r.String(4096);
        std::string asTextEntry = r.String(4096);
        int alEndNum = static_cast<int32_t>(r.U32());
        if(!r.Done()) return false;
        if(!LuxValidateMultiplayerAsset(asMusic,"audio",error)) return false;
        cLuxScriptHandler::StartCredits(asMusic, abLoopMusic, asTextCat, asTextEntry, alEndNum);return true;
    }
    case 2: { // StartDemoEnd
        if(!r.Done()) return false;
        cLuxScriptHandler::StartDemoEnd();return true;
    }
    case 3: { // SetMapDisplayNameEntry
        std::string asNameEntry = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::SetMapDisplayNameEntry(asNameEntry);return true;
    }
    case 4: { // SetSkyBoxActive
        bool abActive = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetSkyBoxActive(abActive);return true;
    }
    case 5: { // SetSkyBoxTexture
        std::string asTexture = r.String(4096);
        if(!r.Done()) return false;
        if(!LuxValidateMultiplayerAsset(asTexture,"cubemap",error)) return false;
        cLuxScriptHandler::SetSkyBoxTexture(asTexture);return true;
    }
    case 6: { // SetSkyBoxColor
        float afR = r.Float();
        float afG = r.Float();
        float afB = r.Float();
        float afA = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetSkyBoxColor(afR, afG, afB, afA);return true;
    }
    case 7: { // SetFogActive
        bool abActive = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetFogActive(abActive);return true;
    }
    case 8: { // SetFogColor
        float afR = r.Float();
        float afG = r.Float();
        float afB = r.Float();
        float afA = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetFogColor(afR, afG, afB, afA);return true;
    }
    case 9: { // SetFogProperties
        float afStart = r.Float();
        float afEnd = r.Float();
        float afFalloffExp = r.Float();
        bool abCulling = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetFogProperties(afStart, afEnd, afFalloffExp, abCulling);return true;
    }
    case 10: { // SetupLoadScreen
        std::string asTextCat = r.String(4096);
        std::string asTextEntry = r.String(4096);
        int alRandomNum = static_cast<int32_t>(r.U32());
        std::string asImageFile = r.String(4096);
        if(!r.Done()) return false;
        if(!LuxValidateMultiplayerAsset(asImageFile,"texture",error)) return false;
        cLuxScriptHandler::SetupLoadScreen(asTextCat, asTextEntry, alRandomNum, asImageFile);return true;
    }
    case 11: { // FadeIn
        float afTime = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::FadeIn(afTime);return true;
    }
    case 12: { // FadeOut
        float afTime = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::FadeOut(afTime);return true;
    }
    case 13: { // FadeImageTrailTo
        float afAmount = r.Float();
        float afSpeed = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::FadeImageTrailTo(afAmount, afSpeed);return true;
    }
    case 14: { // FadeSepiaColorTo
        float afAmount = r.Float();
        float afSpeed = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::FadeSepiaColorTo(afAmount, afSpeed);return true;
    }
    case 15: { // FadeRadialBlurTo
        float afSize = r.Float();
        float afSpeed = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::FadeRadialBlurTo(afSize, afSpeed);return true;
    }
    case 16: { // SetRadialBlurStartDist
        float afStartDist = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetRadialBlurStartDist(afStartDist);return true;
    }
    case 17: { // StartEffectFlash
        float afFadeIn = r.Float();
        float afWhite = r.Float();
        float afFadeOut = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::StartEffectFlash(afFadeIn, afWhite, afFadeOut);return true;
    }
    case 18: { // StartEffectEmotionFlash
        std::string asTextCat = r.String(4096);
        std::string asTextEntry = r.String(4096);
        std::string asSound = r.String(4096);
        if(!r.Done()) return false;
        if(!LuxValidateMultiplayerAsset(asSound,"snt",error)) return false;
        cLuxScriptHandler::StartEffectEmotionFlash(asTextCat, asTextEntry, asSound);return true;
    }
    case 19: { // SetInDarknessEffectsActive
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetInDarknessEffectsActive(abX);return true;
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
        if(!LuxValidateMultiplayerAsset(asVoiceFile,"audio",error)) return false;
        if(!LuxValidateMultiplayerAsset(asEffectFile,"audio",error)) return false;
        cLuxScriptHandler::AddEffectVoice(asVoiceFile, asEffectFile, asTextCat, asTextEntry, abUsePostion, asPosEntity, afMinDistance, afMaxDistance);return true;
    }
    case 21: { // StopAllEffectVoices
        float afFadeOutTime = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::StopAllEffectVoices(afFadeOutTime);return true;
    }
    case 22: { // StartPlayerSpawnPS
        std::string asSPSFile = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::StartPlayerSpawnPS(asSPSFile);return true;
    }
    case 23: { // StopPlayerSpawnPS
        if(!r.Done()) return false;
        cLuxScriptHandler::StopPlayerSpawnPS();return true;
    }
    case 24: { // StartScreenShake
        float afAmount = r.Float();
        float afTime = r.Float();
        float afFadeInTime = r.Float();
        float afFadeOutTime = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::StartScreenShake(afAmount, afTime, afFadeInTime, afFadeOutTime);return true;
    }
    case 25: { // SetInsanitySetEnabled
        std::string asSet = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetInsanitySetEnabled(asSet, abX);return true;
    }
    case 26: { // StartRandomInsanityEvent
        if(!r.Done()) return false;
        cLuxScriptHandler::StartRandomInsanityEvent();return true;
    }
    case 27: { // StartInsanityEvent
        std::string asEventName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::StartInsanityEvent(asEventName);return true;
    }
    case 28: { // StopCurrentInsanityEvent
        if(!r.Done()) return false;
        cLuxScriptHandler::StopCurrentInsanityEvent();return true;
    }
    case 29: { // PlayGuiSound
        std::string asSoundEntFile = r.String(4096);
        float afVolume = r.Float();
        if(!r.Done()) return false;
        tString extension=cString::ToLowerCase(cString::GetFileExt(asSoundEntFile));
        if(!LuxValidateMultiplayerAsset(asSoundEntFile,extension.empty() || extension=="snt" ? "snt" : "audio",error)) return false;
        cLuxScriptHandler::PlayGuiSound(asSoundEntFile, afVolume);return true;
    }
    case 30: { // SetPlayerActive
        bool abActive = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerActive(abActive);return true;
    }
    case 31: { // ChangePlayerStateToNormal
        if(!r.Done()) return false;
        cLuxScriptHandler::ChangePlayerStateToNormal();return true;
    }
    case 32: { // SetPlayerCrouching
        bool abCrouch = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerCrouching(abCrouch);return true;
    }
    case 33: { // AddPlayerBodyForce
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        bool abUseLocalCoords = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::AddPlayerBodyForce(afX, afY, afZ, abUseLocalCoords);return true;
    }
    case 34: { // ShowPlayerCrossHairIcons
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::ShowPlayerCrossHairIcons(abX);return true;
    }
    case 35: { // SetPlayerPos
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerPos(afX, afY, afZ);return true;
    }
    case 36: { // SetPlayerSanity
        float afSanity = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerSanity(afSanity);return true;
    }
    case 37: { // AddPlayerSanity
        float afSanity = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::AddPlayerSanity(afSanity);return true;
    }
    case 38: { // SetPlayerHealth
        float afHealth = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerHealth(afHealth);return true;
    }
    case 39: { // AddPlayerHealth
        float afHealth = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::AddPlayerHealth(afHealth);return true;
    }
    case 40: { // SetPlayerLampOil
        float afOil = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerLampOil(afOil);return true;
    }
    case 41: { // AddPlayerLampOil
        float afOil = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::AddPlayerLampOil(afOil);return true;
    }
    case 42: { // MovePlayerForward
        float afAmount = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::MovePlayerForward(afAmount);return true;
    }
    case 43: { // SetPlayerPermaDeathSound
        std::string asSound = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerPermaDeathSound(asSound);return true;
    }
    case 44: { // SetSanityDrainDisabled
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetSanityDrainDisabled(abX);return true;
    }
    case 45: { // GiveSanityBoost
        if(!r.Done()) return false;
        cLuxScriptHandler::GiveSanityBoost();return true;
    }
    case 46: { // GiveSanityBoostSmall
        if(!r.Done()) return false;
        cLuxScriptHandler::GiveSanityBoostSmall();return true;
    }
    case 47: { // GivePlayerDamage
        float afAmount = r.Float();
        std::string asType = r.String(4096);
        bool abSpinHead = r.U8()!=0;
        bool abLethal = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::GivePlayerDamage(afAmount, asType, abSpinHead, abLethal);return true;
    }
    case 48: { // FadePlayerFOVMulTo
        float afX = r.Float();
        float afSpeed = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::FadePlayerFOVMulTo(afX, afSpeed);return true;
    }
    case 49: { // FadePlayerAspectMulTo
        float afX = r.Float();
        float afSpeed = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::FadePlayerAspectMulTo(afX, afSpeed);return true;
    }
    case 50: { // FadePlayerRollTo
        float afX = r.Float();
        float afSpeedMul = r.Float();
        float afMaxSpeed = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::FadePlayerRollTo(afX, afSpeedMul, afMaxSpeed);return true;
    }
    case 51: { // MovePlayerHeadPos
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        float afSpeed = r.Float();
        float afSlowDownDist = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::MovePlayerHeadPos(afX, afY, afZ, afSpeed, afSlowDownDist);return true;
    }
    case 52: { // StartPlayerLookAt
        std::string asEntityName = r.String(4096);
        float afSpeedMul = r.Float();
        float afMaxSpeed = r.Float();
        std::string asAtTargetCallback = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::StartPlayerLookAt(asEntityName, afSpeedMul, afMaxSpeed, asAtTargetCallback);return true;
    }
    case 53: { // StopPlayerLookAt
        if(!r.Done()) return false;
        cLuxScriptHandler::StopPlayerLookAt();return true;
    }
    case 54: { // SetPlayerMoveSpeedMul
        float afMul = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerMoveSpeedMul(afMul);return true;
    }
    case 55: { // SetPlayerRunSpeedMul
        float afMul = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerRunSpeedMul(afMul);return true;
    }
    case 56: { // SetPlayerLookSpeedMul
        float afMul = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerLookSpeedMul(afMul);return true;
    }
    case 57: { // SetPlayerJumpForceMul
        float afMul = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerJumpForceMul(afMul);return true;
    }
    case 58: { // SetPlayerJumpDisabled
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerJumpDisabled(abX);return true;
    }
    case 59: { // SetPlayerCrouchDisabled
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerCrouchDisabled(abX);return true;
    }
    case 60: { // SetPlayerFallDamageDisabled
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPlayerFallDamageDisabled(abX);return true;
    }
    case 61: { // TeleportPlayer
        std::string asStartPosName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::TeleportPlayer(asStartPosName);return true;
    }
    case 62: { // SetLanternActive
        bool abX = r.U8()!=0;
        bool abUseEffects = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetLanternActive(abX, abUseEffects);return true;
    }
    case 63: { // SetLanternDisabled
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetLanternDisabled(abX);return true;
    }
    case 64: { // SetMessage
        std::string asTextCategory = r.String(4096);
        std::string asTextEntry = r.String(4096);
        float afTime = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetMessage(asTextCategory, asTextEntry, afTime);return true;
    }
    case 65: { // SetDeathHint
        std::string asTextCategory = r.String(4096);
        std::string asTextEntry = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::SetDeathHint(asTextCategory, asTextEntry);return true;
    }
    case 66: { // DisableDeathStartSound
        if(!r.Done()) return false;
        cLuxScriptHandler::DisableDeathStartSound();return true;
    }
    case 67: { // AddNote
        std::string asNameAndTextEntry = r.String(4096);
        std::string asImage = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::AddNote(asNameAndTextEntry, asImage);return true;
    }
    case 68: { // AddDiary
        std::string asNameAndTextEntry = r.String(4096);
        std::string asImage = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::AddDiary(asNameAndTextEntry, asImage);return true;
    }
    case 69: { // ReturnOpenJournal
        bool abOpenJournal = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::ReturnOpenJournal(abOpenJournal);return true;
    }
    case 70: { // AddQuest
        std::string asName = r.String(4096);
        std::string asNameAndTextEntry = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::AddQuest(asName, asNameAndTextEntry);return true;
    }
    case 71: { // CompleteQuest
        std::string asName = r.String(4096);
        std::string asNameAndTextEntry = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::CompleteQuest(asName, asNameAndTextEntry);return true;
    }
    case 72: { // SetNumberOfQuestsInMap
        int alNumberOfQuests = static_cast<int32_t>(r.U32());
        if(!r.Done()) return false;
        cLuxScriptHandler::SetNumberOfQuestsInMap(alNumberOfQuests);return true;
    }
    case 73: { // GiveHint
        std::string asName = r.String(4096);
        std::string asMessageCat = r.String(4096);
        std::string asMessageEntry = r.String(4096);
        float afTimeShown = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::GiveHint(asName, asMessageCat, asMessageEntry, afTimeShown);return true;
    }
    case 74: { // RemoveHint
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::RemoveHint(asName);return true;
    }
    case 75: { // BlockHint
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::BlockHint(asName);return true;
    }
    case 76: { // UnBlockHint
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::UnBlockHint(asName);return true;
    }
    case 77: { // ExitInventory
        if(!r.Done()) return false;
        cLuxScriptHandler::ExitInventory();return true;
    }
    case 78: { // SetInventoryDisabled
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetInventoryDisabled(abX);return true;
    }
    case 79: { // SetInventoryMessage
        std::string asTextCategory = r.String(4096);
        std::string asTextEntry = r.String(4096);
        float afTime = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetInventoryMessage(asTextCategory, asTextEntry, afTime);return true;
    }
    case 80: { // GiveItem
        std::string asName = r.String(4096);
        std::string asType = r.String(4096);
        std::string asSubTypeName = r.String(4096);
        std::string asImageName = r.String(4096);
        float afAmount = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::GiveItem(asName, asType, asSubTypeName, asImageName, afAmount);return true;
    }
    case 81: { // GiveItemFromFile
        std::string asName = r.String(4096);
        std::string asFileName = r.String(4096);
        if(!r.Done()) return false;
        if(!LuxValidateMultiplayerAsset(asFileName,"ent",error)) return false;
        cLuxScriptHandler::GiveItemFromFile(asName, asFileName);return true;
    }
    case 82: { // RemoveItem
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::RemoveItem(asName);return true;
    }
    case 83: { // PreloadParticleSystem
        std::string asPSFile = r.String(4096);
        if(!r.Done()) return false;
        if(!LuxValidateMultiplayerAsset(asPSFile,"ps",error)) return false;
        cLuxScriptHandler::PreloadParticleSystem(asPSFile);return true;
    }
    case 84: { // PreloadSound
        std::string asSoundFile = r.String(4096);
        if(!r.Done()) return false;
        if(!LuxValidateMultiplayerAsset(asSoundFile,"snt",error)) return false;
        cLuxScriptHandler::PreloadSound(asSoundFile);return true;
    }
    case 85: { // CreateParticleSystemAtEntity
        std::string asPSName = r.String(4096);
        std::string asPSFile = r.String(4096);
        std::string asEntity = r.String(4096);
        bool abSavePS = r.U8()!=0;
        if(!r.Done()) return false;
        if(!LuxValidateMultiplayerAsset(asPSFile,"ps",error)) return false;
        cLuxScriptHandler::CreateParticleSystemAtEntity(asPSName, asPSFile, asEntity, abSavePS);return true;
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
        if(!LuxValidateMultiplayerAsset(asPSFile,"ps",error)) return false;
        cLuxScriptHandler::CreateParticleSystemAtEntityExt(asPSName, asPSFile, asEntity, abSavePS, afR, afG, afB, afA, abFadeAtDistance, afFadeMinEnd, afFadeMinStart, afFadeMaxStart, afFadeMaxEnd);return true;
    }
    case 87: { // DestroyParticleSystem
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::DestroyParticleSystem(asName);return true;
    }
    case 88: { // PlaySoundAtEntity
        std::string asSoundName = r.String(4096);
        std::string asSoundFile = r.String(4096);
        std::string asEntity = r.String(4096);
        float afFadeTime = r.Float();
        bool abSaveSound = r.U8()!=0;
        if(!r.Done()) return false;
        if(!LuxValidateMultiplayerAsset(asSoundFile,"snt",error)) return false;
        cLuxScriptHandler::PlaySoundAtEntity(asSoundName, asSoundFile, asEntity, afFadeTime, abSaveSound);return true;
    }
    case 89: { // FadeInSound
        std::string asSoundName = r.String(4096);
        float afFadeTime = r.Float();
        bool abPlayStart = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::FadeInSound(asSoundName, afFadeTime, abPlayStart);return true;
    }
    case 90: { // StopSound
        std::string asSoundName = r.String(4096);
        float afFadeTime = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::StopSound(asSoundName, afFadeTime);return true;
    }
    case 91: { // SetLightVisible
        std::string asLightName = r.String(4096);
        bool abVisible = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetLightVisible(asLightName, abVisible);return true;
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
        cLuxScriptHandler::FadeLightTo(asLightName, afR, afG, afB, afA, afRadius, afTime);return true;
    }
    case 93: { // SetLightFlickerActive
        std::string asLightName = r.String(4096);
        bool abActive = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetLightFlickerActive(asLightName, abActive);return true;
    }
    case 94: { // PlayMusic
        std::string asMusicFile = r.String(4096);
        bool abLoop = r.U8()!=0;
        float afVolume = r.Float();
        float afFadeTime = r.Float();
        int alPrio = static_cast<int32_t>(r.U32());
        bool abResume = r.U8()!=0;
        if(!r.Done()) return false;
        if(!LuxValidateMultiplayerAsset(asMusicFile,"audio",error)) return false;
        cLuxScriptHandler::PlayMusic(asMusicFile, abLoop, afVolume, afFadeTime, alPrio, abResume);return true;
    }
    case 95: { // StopMusic
        float afFadeTime = r.Float();
        int alPrio = static_cast<int32_t>(r.U32());
        if(!r.Done()) return false;
        cLuxScriptHandler::StopMusic(afFadeTime, alPrio);return true;
    }
    case 96: { // FadeGlobalSoundVolume
        float afDestVolume = r.Float();
        float afTime = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::FadeGlobalSoundVolume(afDestVolume, afTime);return true;
    }
    case 97: { // FadeGlobalSoundSpeed
        float afDestSpeed = r.Float();
        float afTime = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::FadeGlobalSoundSpeed(afDestSpeed, afTime);return true;
    }
    case 98: { // SetEntityActive
        std::string asName = r.String(4096);
        bool abActive = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetEntityActive(asName, abActive);return true;
    }
    case 99: { // SetEntityVisible
        std::string asName = r.String(4096);
        bool abVisible = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetEntityVisible(asName, abVisible);return true;
    }
    case 100: { // SetEntityPos
        std::string asName = r.String(4096);
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetEntityPos(asName, afX, afY, afZ);return true;
    }
    case 101: { // SetEntityCustomFocusCrossHair
        std::string asName = r.String(4096);
        std::string asCrossHair = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::SetEntityCustomFocusCrossHair(asName, asCrossHair);return true;
    }
    case 102: { // CreateEntityAtArea
        std::string asEntityName = r.String(4096);
        std::string asEntityFile = r.String(4096);
        std::string asAreaName = r.String(4096);
        bool abFullGameSave = r.U8()!=0;
        if(!r.Done()) return false;
        if(!LuxValidateMultiplayerAsset(asEntityFile,"ent",error)) return false;
        cLuxScriptHandler::CreateEntityAtArea(asEntityName, asEntityFile, asAreaName, abFullGameSave);return true;
    }
    case 103: { // ReplaceEntity
        std::string asName = r.String(4096);
        std::string asBodyName = r.String(4096);
        std::string asNewEntityName = r.String(4096);
        std::string asNewEntityFile = r.String(4096);
        bool abFullGameSave = r.U8()!=0;
        if(!r.Done()) return false;
        if(!LuxValidateMultiplayerAsset(asNewEntityFile,"ent",error)) return false;
        cLuxScriptHandler::ReplaceEntity(asName, asBodyName, asNewEntityName, asNewEntityFile, abFullGameSave);return true;
    }
    case 104: { // PlaceEntityAtEntity
        std::string asName = r.String(4096);
        std::string asTargetEntity = r.String(4096);
        std::string asTargetBodyName = r.String(4096);
        bool abUseRotation = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::PlaceEntityAtEntity(asName, asTargetEntity, asTargetBodyName, abUseRotation);return true;
    }
    case 105: { // SetEntityInteractionDisabled
        std::string asName = r.String(4096);
        bool abDisabled = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetEntityInteractionDisabled(asName, abDisabled);return true;
    }
    case 106: { // SetPropEffectActive
        std::string asName = r.String(4096);
        bool abActive = r.U8()!=0;
        bool abFadeAndPlaySounds = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPropEffectActive(asName, abActive, abFadeAndPlaySounds);return true;
    }
    case 107: { // SetPropActiveAndFade
        std::string asName = r.String(4096);
        bool abActive = r.U8()!=0;
        float afFadeTime = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPropActiveAndFade(asName, abActive, afFadeTime);return true;
    }
    case 108: { // SetPropStaticPhysics
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPropStaticPhysics(asName, abX);return true;
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
        cLuxScriptHandler::RotatePropToSpeed(asName, afAcc, afGoalSpeed, afAxisX, afAxisY, afAxisZ, abResetSpeed, asOffsetArea);return true;
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
        if(!LuxValidateMultiplayerAsset(asAttachFile,"ent",error)) return false;
        cLuxScriptHandler::AttachPropToProp(asPropName, asAttachName, asAttachFile, afPosX, afPosY, afPosZ, afRotX, afRotY, afRotZ);return true;
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
        if(!LuxValidateMultiplayerAsset(asAttachFile,"ent",error)) return false;
        cLuxScriptHandler::AddAttachedPropToProp(asPropName, asAttachName, asAttachFile, afPosX, afPosY, afPosZ, afRotX, afRotY, afRotZ);return true;
    }
    case 112: { // RemoveAttachedPropFromProp
        std::string asPropName = r.String(4096);
        std::string asAttachName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::RemoveAttachedPropFromProp(asPropName, asAttachName);return true;
    }
    case 113: { // SetLampLit
        std::string asName = r.String(4096);
        bool abLit = r.U8()!=0;
        bool abEffects = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetLampLit(asName, abLit, abEffects);return true;
    }
    case 114: { // SetSwingDoorLocked
        std::string asName = r.String(4096);
        bool abLocked = r.U8()!=0;
        bool abEffects = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetSwingDoorLocked(asName, abLocked, abEffects);return true;
    }
    case 115: { // SetSwingDoorClosed
        std::string asName = r.String(4096);
        bool abClosed = r.U8()!=0;
        bool abEffects = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetSwingDoorClosed(asName, abClosed, abEffects);return true;
    }
    case 116: { // SetSwingDoorDisableAutoClose
        std::string asName = r.String(4096);
        bool abDisableAutoClose = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetSwingDoorDisableAutoClose(asName, abDisableAutoClose);return true;
    }
    case 117: { // SetLevelDoorLocked
        std::string asName = r.String(4096);
        bool abLocked = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetLevelDoorLocked(asName, abLocked);return true;
    }
    case 118: { // SetLevelDoorLockedSound
        std::string asName = r.String(4096);
        std::string asSound = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::SetLevelDoorLockedSound(asName, asSound);return true;
    }
    case 119: { // SetLevelDoorLockedText
        std::string asName = r.String(4096);
        std::string asTextCat = r.String(4096);
        std::string asTextEntry = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::SetLevelDoorLockedText(asName, asTextCat, asTextEntry);return true;
    }
    case 120: { // SetPropObjectStuckState
        std::string asName = r.String(4096);
        int alState = static_cast<int32_t>(r.U32());
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPropObjectStuckState(asName, alState);return true;
    }
    case 121: { // SetWheelAngle
        std::string asName = r.String(4096);
        float afAngle = r.Float();
        bool abAutoMove = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetWheelAngle(asName, afAngle, abAutoMove);return true;
    }
    case 122: { // SetWheelStuckState
        std::string asName = r.String(4096);
        int alState = static_cast<int32_t>(r.U32());
        bool afEffects = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetWheelStuckState(asName, alState, afEffects);return true;
    }
    case 123: { // SetLeverStuckState
        std::string asName = r.String(4096);
        int alState = static_cast<int32_t>(r.U32());
        bool afEffects = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetLeverStuckState(asName, alState, afEffects);return true;
    }
    case 124: { // SetWheelInteractionDisablesStuck
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetWheelInteractionDisablesStuck(asName, abX);return true;
    }
    case 125: { // SetLeverInteractionDisablesStuck
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetLeverInteractionDisablesStuck(asName, abX);return true;
    }
    case 126: { // SetMultiSliderStuckState
        std::string asName = r.String(4096);
        int alStuckState = static_cast<int32_t>(r.U32());
        bool abEffects = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetMultiSliderStuckState(asName, alStuckState, abEffects);return true;
    }
    case 127: { // SetButtonSwitchedOn
        std::string asName = r.String(4096);
        bool abSwitchedOn = r.U8()!=0;
        bool abEffects = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetButtonSwitchedOn(asName, abSwitchedOn, abEffects);return true;
    }
    case 128: { // SetAllowStickyAreaAttachment
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetAllowStickyAreaAttachment(abX);return true;
    }
    case 129: { // AttachPropToStickyArea
        std::string asAreaName = r.String(4096);
        std::string asProp = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::AttachPropToStickyArea(asAreaName, asProp);return true;
    }
    case 130: { // AttachBodyToStickyArea
        std::string asAreaName = r.String(4096);
        std::string asBody = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::AttachBodyToStickyArea(asAreaName, asBody);return true;
    }
    case 131: { // DetachFromStickyArea
        std::string asAreaName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::DetachFromStickyArea(asAreaName);return true;
    }
    case 132: { // SetNPCAwake
        std::string asName = r.String(4096);
        bool abAwake = r.U8()!=0;
        bool abEffects = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetNPCAwake(asName, abAwake, abEffects);return true;
    }
    case 133: { // SetNPCFollowPlayer
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetNPCFollowPlayer(asName, abX);return true;
    }
    case 134: { // SetEnemyDisabled
        std::string asName = r.String(4096);
        bool abDisabled = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetEnemyDisabled(asName, abDisabled);return true;
    }
    case 135: { // SetEnemyIsHallucination
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetEnemyIsHallucination(asName, abX);return true;
    }
    case 136: { // FadeEnemyToSmoke
        std::string asName = r.String(4096);
        bool abPlaySound = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::FadeEnemyToSmoke(asName, abPlaySound);return true;
    }
    case 137: { // ShowEnemyPlayerPosition
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::ShowEnemyPlayerPosition(asName);return true;
    }
    case 138: { // AlertEnemyOfPlayerPresence
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::AlertEnemyOfPlayerPresence(asName);return true;
    }
    case 139: { // SetEnemyDisableTriggers
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetEnemyDisableTriggers(asName, abX);return true;
    }
    case 140: { // AddEnemyPatrolNode
        std::string asName = r.String(4096);
        std::string asNodeName = r.String(4096);
        float afWaitTime = r.Float();
        std::string asAnimation = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::AddEnemyPatrolNode(asName, asNodeName, afWaitTime, asAnimation);return true;
    }
    case 141: { // ClearEnemyPatrolNodes
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::ClearEnemyPatrolNodes(asName);return true;
    }
    case 142: { // SetEnemySanityDecreaseActive
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetEnemySanityDecreaseActive(asName, abX);return true;
    }
    case 143: { // TeleportEnemyToNode
        std::string asName = r.String(4096);
        std::string asNodeName = r.String(4096);
        bool abChangeY = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::TeleportEnemyToNode(asName, asNodeName, abChangeY);return true;
    }
    case 144: { // TeleportEnemyToEntity
        std::string asName = r.String(4096);
        std::string asTargetEntity = r.String(4096);
        std::string asTargetBody = r.String(4096);
        bool abChangeY = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::TeleportEnemyToEntity(asName, asTargetEntity, asTargetBody, abChangeY);return true;
    }
    case 145: { // ChangeManPigPose
        std::string asName = r.String(4096);
        std::string asPoseType = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::ChangeManPigPose(asName, asPoseType);return true;
    }
    case 146: { // SetTeslaPigFadeDisabled
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetTeslaPigFadeDisabled(asName, abX);return true;
    }
    case 147: { // SetTeslaPigSoundDisabled
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetTeslaPigSoundDisabled(asName, abX);return true;
    }
    case 148: { // SetTeslaPigEasyEscapeDisabled
        std::string asName = r.String(4096);
        bool abX = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetTeslaPigEasyEscapeDisabled(asName, abX);return true;
    }
    case 149: { // ForceTeslaPigSighting
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::ForceTeslaPigSighting(asName);return true;
    }
    case 150: { // SetMoveObjectState
        std::string asName = r.String(4096);
        float afState = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetMoveObjectState(asName, afState);return true;
    }
    case 151: { // SetMoveObjectStateExt
        std::string asName = r.String(4096);
        float afState = r.Float();
        float afAcc = r.Float();
        float afMaxSpeed = r.Float();
        float afSlowdownDist = r.Float();
        bool abResetSpeed = r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetMoveObjectStateExt(asName, afState, afAcc, afMaxSpeed, afSlowdownDist, abResetSpeed);return true;
    }
    case 152: { // SetPropHealth
        std::string asName = r.String(4096);
        float afHealth = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetPropHealth(asName, afHealth);return true;
    }
    case 153: { // AddPropHealth
        std::string asName = r.String(4096);
        float afHealth = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::AddPropHealth(asName, afHealth);return true;
    }
    case 154: { // ResetProp
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::ResetProp(asName);return true;
    }
    case 155: { // PlayPropAnimation
        std::string asProp = r.String(4096);
        std::string asAnimation = r.String(4096);
        float afFadeTime = r.Float();
        bool abLoop = r.U8()!=0;
        std::string asCallback = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::PlayPropAnimation(asProp, asAnimation, afFadeTime, abLoop, asCallback);return true;
    }
    case 156: { // AddPropForce
        std::string asName = r.String(4096);
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        std::string asCoordSystem = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::AddPropForce(asName, afX, afY, afZ, asCoordSystem);return true;
    }
    case 157: { // AddPropImpulse
        std::string asName = r.String(4096);
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        std::string asCoordSystem = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::AddPropImpulse(asName, afX, afY, afZ, asCoordSystem);return true;
    }
    case 158: { // AddBodyForce
        std::string asName = r.String(4096);
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        std::string asCoordSystem = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::AddBodyForce(asName, afX, afY, afZ, asCoordSystem);return true;
    }
    case 159: { // AddBodyImpulse
        std::string asName = r.String(4096);
        float afX = r.Float();
        float afY = r.Float();
        float afZ = r.Float();
        std::string asCoordSystem = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::AddBodyImpulse(asName, afX, afY, afZ, asCoordSystem);return true;
    }
    case 160: { // BreakJoint
        std::string asName = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::BreakJoint(asName);return true;
    }
    case 161: { // SetBodyMass
        std::string asName = r.String(4096);
        float afMass = r.Float();
        if(!r.Done()) return false;
        cLuxScriptHandler::SetBodyMass(asName, afMass);return true;
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
        cLuxScriptHandler::InteractConnectPropWithRope(asName, asPropName, asRopeName, abInteractOnly, afSpeedMul, afMinSpeed, afMaxSpeed, abInvert, alStatesUsed);return true;
    }
    case 163: { // InteractConnectPropWithMoveObject
        std::string asName = r.String(4096);
        std::string asPropName = r.String(4096);
        std::string asMoveObjectName = r.String(4096);
        bool abInteractOnly = r.U8()!=0;
        bool abInvert = r.U8()!=0;
        int alStatesUsed = static_cast<int32_t>(r.U32());
        if(!r.Done()) return false;
        cLuxScriptHandler::InteractConnectPropWithMoveObject(asName, asPropName, asMoveObjectName, abInteractOnly, abInvert, alStatesUsed);return true;
    }
    case 164: { // ConnectEntities
        std::string asName = r.String(4096);
        std::string asMainEntity = r.String(4096);
        std::string asConnectEntity = r.String(4096);
        bool abInvertStateSent = r.U8()!=0;
        int alStatesUsed = static_cast<int32_t>(r.U32());
        std::string asCallbackFunc = r.String(4096);
        if(!r.Done()) return false;
        cLuxScriptHandler::ConnectEntities(asName, asMainEntity, asConnectEntity, abInvertStateSent, alStatesUsed, asCallbackFunc);return true;
    }
    case 165: { // SetEntityPlayerInteractCallback: advertise interactability, host executes callback.
        std::string name=r.String(4096),callback=r.String(4096);bool remove=r.U8()!=0;
        if(!r.Done()) return false;
        cLuxScriptHandler::SetEntityPlayerInteractCallback(name,callback,remove);return true;
    }
    default: return false;
    }
}
