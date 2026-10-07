// Integration tests against the real ModelEditor definition compiler, world,
// action history and entity serializer. All output stays in the scratch tree.
#include "../../tools/editors/modeleditor/ModelEditor.h"
#include "../../tools/editors/modeleditor/ModelEditorWorld.h"
#include "../../tools/editors/modeleditor/ModelEditorWindowUserSettings.h"
#include "../../tools/editors/modeleditor/ModelEditorWindowAnimations.h"
#include "../../tools/editors/common/DirectoryHandler.h"
#include "../../tools/editors/common/EditorActionHandler.h"
#include "../../tools/editors/common/EditorAction.h"
#include "../../tools/editors/common/EditorUserClassDefinitionManager.h"
#include "impl/LowLevelGraphicsSDL.h"
#include "gui/WidgetTabFrame.h"
#include "../../../amnesia/src/game/LuxEnemy_LlamaAnimations.h"
#include "../../../amnesia/src/game/LuxEnemy_LlamaProfiles.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <crtdbg.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#undef main

namespace fs = std::filesystem;
static int editorChecks = 0;
static FILE* gpCheckLog = NULL;
static void Require(bool abResult, const char* asLabel)
{
    ++editorChecks;
    if(gpCheckLog) { std::fprintf(gpCheckLog, "%s: %s\n", abResult ? "PASS" : "FAIL", asLabel); std::fflush(gpCheckLog); }
    if(abResult) return;
    std::fprintf(stderr, "FAIL: %s\n", asLabel);
    std::exit(2);
}

static std::string Read(const fs::path& aPath)
{
    std::ifstream file(aPath, std::ios::binary);
    Require(file.good(), "read test input");
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static void Write(const fs::path& aPath, const std::string& asText)
{
    std::ofstream file(aPath, std::ios::binary | std::ios::trunc);
    file << asText;
    Require(file.good(), "write scratch fixture");
}

class cScratchDirectories : public cDirectoryHandler
{
public:
    cScratchDirectories(iEditorBase* apEditor, const tWString& asScratch)
        : cDirectoryHandler(apEditor) { msHomeDir = asScratch; }
};

class cScratchModelEditor : public cModelEditor
{
public:
    explicit cScratchModelEditor(const tWString& asScratch)
    {
        hplDelete(mpDirHandler);
        mpDirHandler = hplNew(cScratchDirectories, (this, asScratch));
    }
    void RefreshEditMenu() { UpdateEditMenu(); }
};

class cTestAnimations : public cModelEditorWindowAnimations
{
public:
    explicit cTestAnimations(cModelEditor* apEditor) : cModelEditorWindowAnimations(apEditor) {}
    const tAnimWrapperVec& Draft() { return mvTempAnimations; }
    cWidgetButton* ConfirmButton() { return mpBOK; }
    cWidgetListBox* AnimationList() { return mpListAnimations; }
    cWidgetListBox* EventList() { return mpListEvents; }
    cEditorInputNumber* SpeedInput() { return mpInpAnimSpeed; }
    cEditorInputNumber* EventTimeInput() { return mpInpEventTime; }
};

class cTestSettings : public cModelEditorWindowUserSettings
{
public:
    explicit cTestSettings(cModelEditor* apEditor) : cModelEditorWindowUserSettings(apEditor) {}
    cWidgetComboBox* TypeList() { return mpComboBoxType; }
    cWidgetComboBox* SubTypeList() { return mpComboBoxSubType; }
    cWidgetButton* PresetButton() { return mpButtonApplyRigPreset; }
    iEditorInput* FindInput(const tWString& asName)
    {
        for(std::list<iEditorInput*>::iterator it = mlstInputs.begin(); it != mlstInputs.end(); ++it)
        {
            tWidgetList& children = (*it)->GetHandle()->GetChildren();
            for(tWidgetListIt child = children.begin(); child != children.end(); ++child)
                if((*child)->GetText() == asName)
                {
                    // Production widgets reject events while their tab is
                    // hidden. Select the input's tab as an editor user would.
                    for(iWidget* pParent = (*it)->GetHandle()->GetParent(); pParent; pParent = pParent->GetParent())
                        if(pParent->GetType() == eWidgetType_Tab)
                        {
                            cWidgetTab* pTab = static_cast<cWidgetTab*>(pParent);
                            pTab->GetParentTabFrame()->SetTabOnTop(pTab);
                            break;
                        }
                    return *it;
                }
        }
        return NULL;
    }
};

static tVarValueMap Values(cModelEditorWorld* apWorld)
{
    tVarValueMap values;
    apWorld->GetClass()->SaveValuesToMap(values);
    return values;
}

static tWString Value(cEditorClassInstance* apClass, const char* asName)
{
    cEditorVarInstance* pVar = apClass->GetVarInstance(cString::To16Char(asName));
    Require(pVar != NULL, asName);
    return pVar->GetValue();
}

static void Act(cModelEditor* apEditor, iEditorAction* apAction)
{
    Require(apAction && apAction->Create(), "settings change produces a valid action");
    apEditor->AddAction(apAction);
}

static void RefreshPopup(cTestSettings* apPopup)
{
    // Production editor frames dispatch this event for settings actions.
    // Class revision alone only covers replacing a subtype's variable objects.
    apPopup->OnWorldModify();
    apPopup->Update(0);
}

static bool SameAnimations(const tAnimWrapperVec& aLeft, const tAnimWrapperVec& aRight)
{
    if(aLeft.size() != aRight.size()) return false;
    // Accessors in the production wrappers are non-const; copies keep this
    // comparison independent of world mutation and animation event identities.
    tAnimWrapperVec left = aLeft, right = aRight;
    for(size_t i = 0; i < left.size(); ++i)
    {
        cAnimationWrapper& l = left[i];
        cAnimationWrapper& r = right[i];
        if(l.GetName() != r.GetName() || l.GetFile() != r.GetFile() ||
           l.GetSpeed() != r.GetSpeed() || l.GetSpecialEventTime() != r.GetSpecialEventTime() ||
           l.GetEvents().size() != r.GetEvents().size()) return false;
        for(size_t j = 0; j < l.GetEvents().size(); ++j)
        {
            cAnimationEventWrapper& le = l.GetEvents()[j];
            cAnimationEventWrapper& re = r.GetEvents()[j];
            if(le.GetTime() != re.GetTime() || le.GetType() != re.GetType() || le.GetValue() != re.GetValue()) return false;
        }
    }
    return true;
}

static std::vector<tAnimWrapperVec> LoadRetailAnimations(cEngine* apEngine, const fs::path& aAssetRoot)
{
    const char* files[] = {"entities/enemy/servant_grunt/servant_grunt.ent",
        "entities/enemy/servant_brute/servant_brute.ent", "entities/ptest/enemy_suitor/enemy_suitor.ent", "entities/manpig.ent"};
    std::vector<tAnimWrapperVec> profiles;
    Require(kLuxLlamaAnimationProfileCount == 4, "all four stock animation profiles are available");
    for(size_t i = 0; i < 4; ++i)
    {
        iXmlDocument* pDoc = apEngine->GetResources()->GetLowLevel()->CreateXmlDocument();
        Require(pDoc->CreateFromFile(cString::To16Char((aAssetRoot / files[i]).generic_string())), "read original retail rig entity");
        cXmlElement* pAnimations = pDoc->GetFirstElement("ModelData")->GetFirstElement("Animations");
        Require(pAnimations != NULL, "retail rig has ModelData animation list");
        tAnimWrapperVec animations;
        cXmlNodeListIterator it = pAnimations->GetChildIterator();
        while(it.HasNext())
        {
            cAnimationWrapper animation;
            animation.Load(it.Next()->ToElement());
            animations.push_back(animation);
        }
        hplDelete(pDoc);
        const cLuxLlamaAnimationProfile& profile = kLuxLlamaAnimationProfiles[i];
        Require(animations.size() == profile.mlAnimationCount, "canonical clip count matches original rig");
        for(size_t j = 0; j < animations.size(); ++j)
        {
            cAnimationWrapper& actual = animations[j];
            const cLuxLlamaAnimation& expected = profile.mpAnimations[j];
            Require(actual.GetName() == expected.msName && actual.GetFile() == expected.msFile &&
                std::fabs(actual.GetSpeed() - expected.mfSpeed) < 0.000001f &&
                std::fabs(actual.GetSpecialEventTime() - expected.mfSpecialEventTime) < 0.000001f,
                "canonical clip name path speed and special event match retail");
            Require(actual.GetEvents().size() == expected.mlEventCount, "canonical event count matches retail clip");
            for(size_t e = 0; e < actual.GetEvents().size(); ++e)
                Require(std::fabs(actual.GetEvents()[e].GetTime() - expected.mpEvents[e].mfTime) < 0.000001f &&
                    actual.GetEvents()[e].GetType() == expected.mpEvents[e].msType &&
                    actual.GetEvents()[e].GetValue() == expected.mpEvents[e].msValue,
                    "canonical ordered event time type and value match retail");
            if(i < 3)
            {
                const tWString& resolved = apEngine->GetResources()->GetFileSearcher()->GetFilePath(expected.msFile);
                Require(!resolved.empty(), "stock relative clip path resolves through engine file searcher");
                cAnimation* pAnimation = apEngine->GetResources()->GetAnimationManager()->CreateAnimation(expected.msFile);
                Require(pAnimation && pAnimation->GetTrackNum() > 0 && pAnimation->GetLength() > 0, "actual stock clip loads through engine animation manager");
                apEngine->GetResources()->GetAnimationManager()->Destroy(pAnimation);
            }
        }
        profiles.push_back(animations);
    }
    return profiles;
}

static void CompileCustomDefinition(cModelEditor* apEditor, const fs::path& aScratch)
{
    // A mod's existing Enemy_Llama must remain authoritative. Using the normal
    // filename exercises the same augmentation path as ModelEditor startup.
    const fs::path customDir = aScratch / "custom-schema";
    fs::create_directories(customDir);
    const fs::path customFile = customDir / "EntityTypes.cfg";
    Write(customFile, "<UserClassDefinition><Types><Type Name=\"Enemy_Llama\" Default=\"Custom\"><TypeVars><Group Name=\"Custom settings\"><Var Name=\"CustomMarker\" Type=\"String\" DefaultValue=\"preserved\" /></Group></TypeVars><InstanceVars><Group Name=\"Instance one\"><Var Name=\"InstanceFirst\" Type=\"Float\" DefaultValue=\"1.25\" /></Group><Group Name=\"Instance two\"><Var Name=\"InstanceSecond\" Type=\"Bool\" DefaultValue=\"true\" /></Group></InstanceVars><SubTypes><SubType Name=\"Custom\" /></SubTypes></Type></Types></UserClassDefinition>");
    const std::string original = Read(customFile);
    apEditor->GetEngine()->GetResources()->AddResourceDir(cString::To16Char(customDir.generic_string()), false);
    cEditorUserClassDefinitionManager manager(apEditor);
    cEditorUserClassDefinition definition(&manager);
    Require(definition.Create(customFile.generic_string(), eEditorVarCategory_Type | eEditorVarCategory_Instance), "compile existing custom Enemy_Llama definition");
    Require(definition.GetTypeNum() == 1, "existing Enemy_Llama is not duplicated");
    cEditorUserClassType* pType = definition.GetType("Enemy_Llama");
    Require(pType && pType->GetSubTypeNum() == 1 && pType->GetSubType("Custom"), "custom subtypes remain authoritative");
    cEditorClassInstance* pInstance = pType->GetSubType("Custom")->CreateInstance(eEditorVarCategory_Type);
    Require(Value(pInstance, "CustomMarker") == _W("preserved"), "grouped custom fields retain defaults");
    Require(pInstance->GetVarInstance(_W("InstanceFirst")) == NULL && pInstance->GetVarInstance(_W("InstanceSecond")) == NULL, "grouped instance variables do not leak into type category");
    hplDelete(pInstance);
    pInstance = pType->GetSubType("Custom")->CreateInstance(eEditorVarCategory_Instance);
    Require(Value(pInstance, "InstanceFirst") == _W("1.25") && Value(pInstance, "InstanceSecond") == _W("true"), "both grouped instance categories compile");
    Require(pInstance->GetVarInstance(_W("CustomMarker")) == NULL, "type group does not leak into instance category");
    hplDelete(pInstance);
    Require(Read(customFile) == original, "definition augmentation never edits source config");
}

int main(int argc, char** argv)
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    Require(argc == 3, "supply read-only asset root and scratch output directory");
    const fs::path assetRoot = fs::absolute(argv[1]);
    const fs::path scratch = fs::absolute(argv[2]);
    fs::create_directories(scratch);
    gpCheckLog = std::fopen((scratch / "checks.log").string().c_str(), "w");
    fs::create_directories(scratch / "personal");
    const std::string retailDefinition = Read(assetRoot / "editor" / "EntityTypes.cfg");
    for(const char* directory : {"core", "editor"})
        fs::copy(assetRoot / directory, scratch / directory, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    fs::copy_file(assetRoot / "materials.cfg", scratch / "materials.cfg", fs::copy_options::overwrite_existing);
    for(const char* directory : {"lights", "billboards", "particles", "sounds", "maps"})
        fs::create_directories(scratch / directory);
    std::string resources = "<Resources>";
    for(const char* directory : {"fonts", "gui", "entities", "textures", "models", "graphics", "viewer", "sounds", "particles", "lights", "billboards"})
        resources += "<Directory Path=\"" + (assetRoot / directory).generic_string() + "\" AddSubDirs=\"true\" />";
    resources += "<Directory Path=\"" + assetRoot.generic_string() + "\" AddSubDirs=\"false\" /></Resources>";
    Write(scratch / "resources.cfg", resources);
    Write(scratch / "MainEditorSettings.cfg", "<Directories EditorHomeDir=\"Profile\" EditorTempDir=\"Temp\" ThumbnailsDir=\"Thumbnails\" />");
    fs::current_path(scratch);

    // DirectoryHandler checks this existing game folder in its constructor.
    // Fail before constructing the editor rather than creating user folders.
    Require(cPlatform::FolderExists(cPlatform::GetSystemSpecialPath(eSystemPath_Personal) + _W("Amnesia/")), "existing game personal directory is required");
    cResources::SetForceCacheLoadingAndSkipSaving(true);
    cEngineInitVars vars;
    vars.mGraphics.mvScreenSize = cVector2l(1024, 768);
    vars.mGraphics.mvWindowPosition = cVector2l(-10000, -10000);
    vars.mGraphics.msWindowCaption = "Enemy_Llama Editor Test";
    cEngine* pEngine = CreateHPLEngine(eHplAPI_OpenGL, eHplSetup_All, &vars);
    Require(pEngine != NULL, "initialize native OpenGL editor engine");
    for(Uint32 i = 1; i < 16; ++i)
        if(SDL_Window* pWindow = SDL_GetWindowFromID(i)) SDL_HideWindow(pWindow);
    Require(pEngine->GetResources()->LoadResourceDirsFile("resources.cfg"), "load read-only retail resource paths");
    cScratchModelEditor* pEditor = hplNew(cScratchModelEditor, (cString::To16Char((scratch / "personal").generic_string() + "/")));
    Require(pEditor->Init(pEngine, "LlamaEditorTest", "test", false) == pEngine, "initialize real ModelEditor");
    Require(fs::equivalent(fs::path(cString::To8Char(pEditor->GetHomeDir())), scratch / "personal" / "Profile"), "editor config home stays in scratch");

    cModelEditorWorld* pWorld = static_cast<cModelEditorWorld*>(pEditor->GetEditorWorld());
    cEditorUserClassDefinition* pDefinition = pEditor->GetClassDefinitionManager()->GetDefinition(eUserClassDefinition_Entity);
    cEditorUserClassType* pLlama = pDefinition->GetType("Enemy_Llama");
    Require(pLlama && pLlama->GetSubTypeNum() == 4, "automatic Enemy_Llama registration includes all four rigs");
    const char* rigNames[] = {"Grunt", "Brute", "Suitor", "ManPig"};
    const char* bodySizes[] = {"1.2 1.85 1.2", "1.5 1.85 1.5", "1.5 1.85 1.5", "1.2 1.85 1.2"};
    Require(pLlama->GetSubType("") == pLlama->GetSubType("Grunt") && pLlama->GetSubType("unknown") == pLlama->GetSubType("Grunt"), "missing and unknown Llama subtypes default to Grunt");
    Require(pLlama->GetSubType("brute") == pLlama->GetSubType("Brute"), "Llama subtype names are case insensitive");
    for(int i = 0; i < 4; ++i)
    {
        cEditorUserClassSubType* pSubtype = pLlama->GetSubType(rigNames[i]);
        Require(pSubtype != NULL, rigNames[i]);
        cEditorClassInstance* pInstance = pSubtype->CreateInstance(eEditorVarCategory_Type);
        Require(pInstance->GetVarInstanceNum() == 75, "each grouped rig schema compiles all 75 fields");
        const cLuxLlamaProfile& profile = kLuxLlamaProfiles[i];
        Require(profile.mlVariableCount == 62, "each physical profile has 62 settings");
        for(size_t j = 0; j < profile.mlVariableCount; ++j)
            Require(Value(pInstance, profile.mpVariables[j].msName) == cString::To16Char(profile.mpVariables[j].msValue), "all body movement health attack and animation defaults survive definition compilation");
        Require(Value(pInstance, "Body_Size") == cString::To16Char(bodySizes[i]), "rig physical defaults compile through grouped fields");
        Require(Value(pInstance, "LlamaObservationWidth") == _W("1280") && Value(pInstance, "LlamaObservationHeight") == _W("864"), "observation defaults available on every rig");
        Require(Value(pInstance, "FOV") == _W("120"), "horizontal FOV defaults to 120 degrees on every rig");
        Require(Value(pInstance, "LlamaControlEnabled") == _W("true") &&
            Value(pInstance, "LlamaDecisionInterval") == _W("1.0") &&
            Value(pInstance, "LlamaActionMaxSeconds") == _W("1.5") &&
            Value(pInstance, "LlamaAttackCooldown") == _W("1.0"),
            "model control and bounded action defaults are available on every rig");
        Require(Value(pInstance, "LlamaAttackAnimation").empty() &&
            Value(pInstance, "LlamaDoorAttackAnimation").empty(),
            "attack animation overrides start empty on every rig");
        Require(pInstance->GetVarInstance(_W("SightRange")) == NULL && pInstance->GetVarInstance(_W("DarknessSightRange")) == NULL, "Llama schema omits legacy detection knobs");
        hplDelete(pInstance);
    }
    Require(Read(scratch / "editor" / "EntityTypes.cfg") == retailDefinition, "automatic registration leaves disk schema unchanged");

    const std::vector<tAnimWrapperVec> retailAnimations = LoadRetailAnimations(pEngine, assetRoot);
    for(int i = 0; i < 4; ++i)
    {
        pWorld->SetType(pLlama->GetSubType(rigNames[i]), false);
        Require(pWorld->ApplyRigPreset(), "each rig can apply its complete stock preset");
        Require(SameAnimations(pWorld->GetAnimations(), retailAnimations[i]), "each applied preset restores exact retail clips paths and events");
        Require(!pWorld->ApplyRigPreset(), "each identical complete preset is a no-op");
    }
    pEditor->GetActionHandler()->Reset();
    pWorld->Reset();

    iXmlDocument* pSource = pEngine->GetResources()->LoadXmlDocument("servant_grunt.ent");
    Require(pSource && pWorld->Load(pSource), "load real rig entity in native ModelEditor world");
    pEngine->GetResources()->DestroyXmlDocument(pSource);
    Require(pWorld->GetMesh() != NULL, "loaded entity includes actual rigged mesh");
    const tWString copiedFOV = Value(pWorld->GetClass(), "FOV");
    const tString meshName = pWorld->GetMeshFilename();
    tAnimWrapperVec editedAnimations = pWorld->GetAnimations();
    Require(!editedAnimations.empty(), "fixture includes animations to customize");
    editedAnimations[0].SetName("CustomIdle");
    editedAnimations[0].SetFile("custom/custom_idle.anm");
    editedAnimations[0].SetSpeed(1.25f);
    editedAnimations[0].SetSpecialEventTime(0.75f);
    cAnimationEventWrapper event;
    event.SetTime(0.125f);
    event.SetType("Sound");
    event.SetValue("custom/edited_event");
    editedAnimations[0].GetEvents().push_back(event);
    pWorld->SetAnimations(editedAnimations);
    const unsigned int beforePopup = pWorld->GetNumModifications();
    cTestSettings* pPopup = hplNew(cTestSettings, (pEditor));
    pPopup->Init();
    pEditor->AddWindow(pPopup);
    pPopup->SetActive(true);
    Require(pWorld->GetNumModifications() == beforePopup, "opening actual user settings popup adds no modifications");
    Require(!pPopup->PresetButton()->IsVisible(), "rig preset control is hidden for ordinary enemy type");
    pPopup->TypeList()->SetSelectedItem(pLlama->GetIndex());
    Require(pWorld->GetClass()->GetClass() == pLlama->GetSubType("Grunt") && pPopup->SubTypeList()->GetItemNum() == 4, "actual type dropdown selects Llama and exposes four rig profiles");
    Require(pPopup->PresetButton()->IsVisible() && pPopup->PresetButton()->IsEnabled(), "preset control appears for supported Llama rig");
    Require(Value(pWorld->GetClass(), "FOV") == copiedFOV, "changing copied enemy type preserves its existing FOV");
    iEditorInput* pFOVInput = pPopup->FindInput(_W("FOV"));
    Require(pFOVInput != NULL, "actual popup renders horizontal FOV input");
    pFOVInput->SetValue(_W("135"), true);
    Require(Value(pWorld->GetClass(), "FOV") == _W("135"), "actual FOV input creates an undoable observation edit");
    pEditor->GetActionHandler()->Undo();
    RefreshPopup(pPopup);
    Require(Value(pWorld->GetClass(), "FOV") == copiedFOV &&
        cString::ToFloat(cString::To8Char(pPopup->FindInput(_W("FOV"))->GetValue()).c_str(), 0) ==
        cString::ToFloat(cString::To8Char(copiedFOV).c_str(), 0),
        "FOV undo restores copied value and displayed input");
    pEditor->GetActionHandler()->Redo();
    RefreshPopup(pPopup);
    Require(Value(pWorld->GetClass(), "FOV") == _W("135"), "FOV redo restores the configured observation angle");
    iEditorInput* pControlInput = pPopup->FindInput(_W("LlamaControlEnabled"));
    Require(pControlInput != NULL, "actual popup renders model control checkbox");
    pControlInput->SetValue(_W("false"), true);
    Require(Value(pWorld->GetClass(), "LlamaControlEnabled") == _W("false"), "model control checkbox creates an undoable setting edit");
    pEditor->GetActionHandler()->Undo();
    RefreshPopup(pPopup);
    Require(Value(pWorld->GetClass(), "LlamaControlEnabled") == _W("true") &&
        pPopup->FindInput(_W("LlamaControlEnabled"))->GetValue() == _W("true"),
        "model control undo restores both value and checkbox");
    pEditor->GetActionHandler()->Redo();
    RefreshPopup(pPopup);
    Require(Value(pWorld->GetClass(), "LlamaControlEnabled") == _W("false"), "model control redo restores configured value");
    iEditorInput* pWidthInput = pPopup->FindInput(_W("LlamaObservationWidth"));
    Require(pWidthInput != NULL, "actual popup renders grouped observation input");
    pWidthInput->SetValue(_W("512"), true);
    Require(Value(pWorld->GetClass(), "LlamaObservationWidth") == _W("512"), "actual input callback creates undoable variable edit");
    pEditor->GetActionHandler()->Undo();
    RefreshPopup(pPopup);
    Require(Value(pWorld->GetClass(), "LlamaObservationWidth") == _W("1280") && cString::ToInt(cString::To8Char(pPopup->FindInput(_W("LlamaObservationWidth"))->GetValue()).c_str(), 0) == 1280, "popup undo refresh restores displayed variable value");
    pEditor->GetActionHandler()->Redo();
    RefreshPopup(pPopup);
    Act(pEditor, pWorld->CreateActionSetVariable(_W("LlamaCameraOffset"), _W("0 -0.25 0")));
    Act(pEditor, pWorld->CreateActionSetVariable(_W("LlamaDecisionInterval"), _W("0.75")));
    Act(pEditor, pWorld->CreateActionSetVariable(_W("LlamaActionMaxSeconds"), _W("0.8")));
    Act(pEditor, pWorld->CreateActionSetVariable(_W("LlamaAttackAnimation"), _W("AttackShort")));
    Act(pEditor, pWorld->CreateActionSetVariable(_W("LlamaDoorAttackAnimation"), _W("BreakDoor")));
    Act(pEditor, pWorld->CreateActionSetVariable(_W("LlamaAttackCooldown"), _W("1.25")));
    Act(pEditor, pWorld->CreateActionSetVariable(_W("Health"), _W("137")));
    const tVarValueMap gruntValues = Values(pWorld);
    const tVarValueMap gruntTemp = pWorld->GetTempValues();
    const unsigned int beforeSubtype = pWorld->GetNumModifications();
    pPopup->SubTypeList()->SetSelectedItem(pLlama->GetSubType("Brute")->GetIndex());
    Require(SameAnimations(pWorld->GetAnimations(), editedAnimations), "ordinary subtype change preserves custom clips and events");
    Require(Value(pWorld->GetClass(), "Health") == _W("137"), "changing rig preserves shared manual overrides");
    pEditor->GetActionHandler()->Undo();
    RefreshPopup(pPopup);
    Require(Values(pWorld) == gruntValues && pWorld->GetTempValues() == gruntTemp && pWorld->GetClass()->GetClass() == pLlama->GetSubType("Grunt"), "subtype undo restores exact values and temporary cache");
    Require(pWorld->GetNumModifications() == beforeSubtype, "subtype undo restores dirty count");
    pEditor->GetActionHandler()->Redo();
    RefreshPopup(pPopup);
    const tVarValueMap beforePreset = Values(pWorld);
    const tVarValueMap beforePresetTemp = pWorld->GetTempValues();
    const unsigned int beforePresetDirty = pWorld->GetNumModifications();
    pPopup->PresetButton()->ProcessMessage(eGuiMessage_ButtonPressed, cGuiMessageData());
    Require(pWorld->GetNumModifications() == beforePresetDirty + 1, "actual preset button adds one settings action");
    Require(Value(pWorld->GetClass(), "Health") == _W("100") && Value(pWorld->GetClass(), "Body_Size") == _W("1.5 1.85 1.5"), "preset replaces mechanical overrides with selected rig defaults");
    Require(Value(pWorld->GetClass(), "LlamaObservationWidth") == _W("512") && Value(pWorld->GetClass(), "LlamaCameraOffset") == _W("0 -0.25 0"), "preset preserves observation overrides");
    Require(Value(pWorld->GetClass(), "FOV") == _W("135"), "rig preset preserves configured observation FOV");
    const char* controlNames[] = {"LlamaControlEnabled", "LlamaDecisionInterval", "LlamaActionMaxSeconds", "LlamaAttackAnimation", "LlamaDoorAttackAnimation", "LlamaAttackCooldown"};
    for(size_t i = 0; i < sizeof(controlNames) / sizeof(controlNames[0]); ++i)
        Require(Value(pWorld->GetClass(), controlNames[i]) == beforePreset.find(cString::To16Char(controlNames[i]))->second,
            "rig preset preserves customized model control and attack settings");
    Require(SameAnimations(pWorld->GetAnimations(), retailAnimations[1]), "actual preset button replaces custom clips with exact Brute retail animation list");
    const tVarValueMap afterPreset = Values(pWorld);
    pEditor->GetActionHandler()->Undo();
    RefreshPopup(pPopup);
    Require(Values(pWorld) == beforePreset && pWorld->GetTempValues() == beforePresetTemp && pWorld->GetNumModifications() == beforePresetDirty, "preset undo restores exact values cache and dirty count");
    Require(SameAnimations(pWorld->GetAnimations(), editedAnimations), "preset undo restores every custom clip and event field");
    pEditor->GetActionHandler()->Redo();
    RefreshPopup(pPopup);
    Require(Values(pWorld) == afterPreset, "preset redo reproduces edited settings");
    Require(SameAnimations(pWorld->GetAnimations(), retailAnimations[1]), "preset redo restores exact stock clips and events");
    Require(!pWorld->ApplyRigPreset(), "applying unchanged preset does not add a no-op action");

    tAnimWrapperVec manualAnimations = pWorld->GetAnimations();
    manualAnimations[0].SetSpeed(0.5f);
    manualAnimations[0].SetSpecialEventTime(0.25f);
    manualAnimations[0].GetEvents().push_back(event);
    const unsigned int beforeManualAnimations = pWorld->GetNumModifications();
    Act(pEditor, pWorld->CreateActionSetAnimations(manualAnimations));
    Require(SameAnimations(pWorld->GetAnimations(), manualAnimations) && pWorld->GetNumModifications() == beforeManualAnimations + 1, "manual animation edit after preset is a single undoable action");
    pEditor->GetActionHandler()->Undo();
    Require(SameAnimations(pWorld->GetAnimations(), retailAnimations[1]) && pWorld->GetNumModifications() == beforeManualAnimations, "undo manual clip edit restores stock preset and dirty count");
    pEditor->GetActionHandler()->Undo();
    Require(SameAnimations(pWorld->GetAnimations(), editedAnimations) && Values(pWorld) == beforePreset && pWorld->GetNumModifications() == beforePresetDirty, "second undo restores pre-preset clips settings and dirty count");
    pEditor->GetActionHandler()->Redo();
    Require(SameAnimations(pWorld->GetAnimations(), retailAnimations[1]), "first redo restores stock preset");
    pEditor->GetActionHandler()->Redo();
    Require(SameAnimations(pWorld->GetAnimations(), manualAnimations), "second redo restores manual clip and event edits");
    iEditorAction* pNoOpAnimation = pWorld->CreateActionSetAnimations(manualAnimations);
    Require(pNoOpAnimation && !pNoOpAnimation->Create(), "identical manual animation edit is a no-op");
    hplDelete(pNoOpAnimation);
    pEditor->GetActionHandler()->Undo();
    RefreshPopup(pPopup);

    cTestAnimations* pAnimationsPopup = hplNew(cTestAnimations, (pEditor));
    pAnimationsPopup->Init();
    pEditor->AddWindow(pAnimationsPopup);
    pAnimationsPopup->SetActive(true);
    pAnimationsPopup->Update(0);
    pEngine->GetGui()->SetFocus(pEditor->GetSet());
    pAnimationsPopup->AnimationList()->SetSelectedItem(0);
    Require(SameAnimations(pAnimationsPopup->Draft(), retailAnimations[1]), "animation dialog starts with committed preset clips");
    pEditor->RefreshEditMenu();
    Require(pEngine->GetGui()->SendKeyPress(cKeyPress(eKey_Z, 0, eKeyModifier_Ctrl)), "global undo shortcut runs with animation popup active");
    Require(SameAnimations(pWorld->GetAnimations(), editedAnimations), "global undo restores clips before the preset");
    pAnimationsPopup->OnWorldModify();
    pAnimationsPopup->Update(0);
    Require(SameAnimations(pAnimationsPopup->Draft(), editedAnimations), "active animation dialog follows preset undo instead of retaining a stale draft");
    Require(pAnimationsPopup->AnimationList()->GetSelectedItem() == 0 && pAnimationsPopup->SpeedInput()->GetValue() == 1.25f,
        "animation dialog refreshes selected clip inputs after undo");
    pEditor->RefreshEditMenu();
    Require(pEngine->GetGui()->SendKeyPress(cKeyPress(eKey_Y, 0, eKeyModifier_Ctrl)), "global redo shortcut runs with animation popup active");
    pAnimationsPopup->OnWorldModify();
    pAnimationsPopup->Update(0);
    Require(SameAnimations(pAnimationsPopup->Draft(), retailAnimations[1]), "active animation dialog follows preset redo");
    pEditor->RefreshEditMenu();
    Require(pEngine->GetGui()->SendKeyPress(cKeyPress(eKey_Z, 0, eKeyModifier_Ctrl)), "undo before confirming animation dialog");
    const unsigned int beforeStaleConfirmation = pWorld->GetNumModifications();
    // Confirm before the next editor frame delivers OnWorldModify.
    pAnimationsPopup->ConfirmButton()->ProcessMessage(eGuiMessage_ButtonPressed, cGuiMessageData());
    Require(SameAnimations(pWorld->GetAnimations(), editedAnimations) && pWorld->GetNumModifications() == beforeStaleConfirmation,
        "animation dialog confirmation cannot reapply stale clips before frame refresh");
    pEditor->RefreshEditMenu();
    Require(pEngine->GetGui()->SendKeyPress(cKeyPress(eKey_Y, 0, eKeyModifier_Ctrl)), "confirmation after undo preserves redo history");
    Act(pEditor, pWorld->CreateActionSetAnimations(manualAnimations));
    pAnimationsPopup->SetActive(true);
    pAnimationsPopup->Update(0);
    pAnimationsPopup->AnimationList()->SetSelectedItem(1);
    pAnimationsPopup->EventList()->SetSelectedItem(0);
    pEditor->RefreshEditMenu();
    Require(pEngine->GetGui()->SendKeyPress(cKeyPress(eKey_Z, 0, eKeyModifier_Ctrl)), "undo committed manual animation edit with dialog active");
    pAnimationsPopup->EventTimeInput()->SetValue(0.999f, true);
    Require(SameAnimations(pAnimationsPopup->Draft(), retailAnimations[1]), "stale animation event input is ignored before the next frame refresh");
    pAnimationsPopup->OnWorldModify();
    pAnimationsPopup->Update(0);
    Require(SameAnimations(pAnimationsPopup->Draft(), retailAnimations[1]), "animation dialog follows undo of committed manual clips");
    pEditor->RefreshEditMenu();
    Require(pEngine->GetGui()->SendKeyPress(cKeyPress(eKey_Y, 0, eKeyModifier_Ctrl)), "redo committed manual animation edit with dialog active");
    pAnimationsPopup->OnWorldModify();
    pAnimationsPopup->Update(0);
    Require(SameAnimations(pAnimationsPopup->Draft(), manualAnimations), "animation dialog follows redo of committed manual clips");
    pAnimationsPopup->AnimationList()->SetSelectedItem(0);
    pAnimationsPopup->SpeedInput()->SetValue(2.0f, true);
    tAnimWrapperVec expectedDraft = manualAnimations;
    expectedDraft[0].SetSpeed(2.0f);
    pAnimationsPopup->AnimationList()->SetSelectedItem(1);
    pAnimationsPopup->EventList()->SetSelectedItem(0);
    pAnimationsPopup->EventTimeInput()->SetValue(0.333f, true);
    expectedDraft[1].GetEvents()[0].SetTime(0.333f);
    Require(pAnimationsPopup->EventList()->GetSelectedItem() == 0, "ordinary event editing preserves the selected event");
    Act(pEditor, pWorld->CreateActionSetVariable(_W("Health"), _W("101")));
    pAnimationsPopup->OnWorldModify();
    Require(pAnimationsPopup->EventList()->GetSelectedItem() == 0 && pAnimationsPopup->EventTimeInput()->GetValue() == 0.333f,
        "unrelated world notification preserves selected pending animation event");
    pAnimationsPopup->Update(0);
    Require(SameAnimations(pAnimationsPopup->Draft(), expectedDraft) && SameAnimations(pWorld->GetAnimations(), manualAnimations),
        "unrelated settings modification preserves pending animation draft");
    pAnimationsPopup->ConfirmButton()->ProcessMessage(eGuiMessage_ButtonPressed, cGuiMessageData());
    Require(SameAnimations(pWorld->GetAnimations(), expectedDraft), "actual animation dialog confirmation commits its current draft");
    pEditor->GetActionHandler()->Undo();
    pEditor->GetActionHandler()->Undo();
    pEditor->GetActionHandler()->Undo();
    RefreshPopup(pPopup);
    Require(SameAnimations(pWorld->GetAnimations(), retailAnimations[1]) && Values(pWorld) == afterPreset,
        "popup regression restores preset settings and clips after undoing its temporary edits");

    iEditorInput* pStaleInput = pPopup->FindInput(_W("LlamaObservationWidth"));
    Require(pStaleInput != NULL, "retain old popup input for stale-event test");
    Act(pEditor, pWorld->CreateActionSetType(pLlama->GetSubType("ManPig")));
    const unsigned int afterExternalTypeChange = pWorld->GetNumModifications();
    pStaleInput->SetValue(_W("999"), true);
    Require(pWorld->GetNumModifications() == afterExternalTypeChange && Value(pWorld->GetClass(), "LlamaObservationWidth") == _W("512"), "stale popup event is ignored before dereferencing replaced class");
    RefreshPopup(pPopup);
    Require(pPopup->SubTypeList()->GetSelectedItem() == pLlama->GetSubType("ManPig")->GetIndex(), "popup refresh follows external subtype change");
    pEditor->GetActionHandler()->Undo();
    RefreshPopup(pPopup);

    iXmlDocument* pSaved = pEngine->GetResources()->GetLowLevel()->CreateXmlDocument();
    Require(pWorld->Save(pSaved), "serialize editor world with Enemy_Llama settings");
    cXmlElement* pVariables = pSaved->GetFirstElement("UserDefinedVariables");
    Require(pVariables && pVariables->GetAttributeString("EntityType") == "Enemy_Llama" && pVariables->GetAttributeString("EntitySubType") == "Brute", "saved entity has game-loader type and selected profile");
    const fs::path savedFile = scratch / "entities" / "enemy" / "servant_brute" / "enemy_llama_roundtrip.ent";
    fs::create_directories(savedFile.parent_path());
    Require(pSaved->SaveToFile(cString::To16Char(savedFile.generic_string())), "save standalone entity in scratch");
    hplDelete(pSaved);
    pPopup->SetActive(false);
    pEditor->GetActionHandler()->Reset();
    pWorld->Reset();
    iXmlDocument* pReloaded = pEngine->GetResources()->GetLowLevel()->CreateXmlDocument();
    Require(pReloaded->CreateFromFile(cString::To16Char(savedFile.generic_string())) && pWorld->Load(pReloaded), "reload saved entity through native editor world");
    hplDelete(pReloaded);
    Require(pWorld->GetClass()->GetClass() == pLlama->GetSubType("Brute") && Values(pWorld) == afterPreset, "entity roundtrip preserves subtype and all visible settings");
    Require(Value(pWorld->GetClass(), "FOV") == _W("135"), "saved entity roundtrip preserves custom observation FOV");
    for(size_t i = 0; i < sizeof(controlNames) / sizeof(controlNames[0]); ++i)
        Require(Value(pWorld->GetClass(), controlNames[i]) == beforePreset.find(cString::To16Char(controlNames[i]))->second,
            "saved entity roundtrip preserves custom model control and attack settings");
    Require(pWorld->GetMesh() && pWorld->GetMeshFilename() == meshName && SameAnimations(pWorld->GetAnimations(), retailAnimations[1]), "entity roundtrip retains mesh and exact stock clip paths and events");
    Require(!pWorld->IsModified(), "loaded roundtrip starts clean");
    CompileCustomDefinition(pEditor, scratch);
    Require(Read(assetRoot / "editor" / "EntityTypes.cfg") == retailDefinition, "retail schema remains unchanged");
    hplDelete(pEditor);
    DestroyHPLEngine(pEngine);
    std::printf("PASS: %d checks using native ModelEditor definition/actions/entity roundtrip\n", editorChecks);
    if(gpCheckLog) std::fclose(gpCheckLog);
    return 0;
}

// The engine's Windows platform object references its application entry point.
// This consumer uses the console main above so CTest can pass explicit paths.
int hplMain(const tString&) { return 1; }
