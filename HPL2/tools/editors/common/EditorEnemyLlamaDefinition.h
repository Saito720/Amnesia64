/* Enemy_Llama editor definition. GPL-3.0-or-later. */
#ifndef HPL_EDITOR_ENEMY_LLAMA_DEFINITION_H
#define HPL_EDITOR_ENEMY_LLAMA_DEFINITION_H

#include "resources/XmlDocument.h"
#include "../../../../amnesia/src/game/LuxEnemy_LlamaProfiles.h"
#include <cstring>

namespace EditorEnemyLlamaDefinition
{
    inline bool StartsWith(const char* asName, const char* asPrefix)
    {
        return std::strncmp(asName, asPrefix, std::strlen(asPrefix)) == 0;
    }

    inline bool EndsWith(const char* asName, const char* asSuffix)
    {
        const std::size_t lNameLength = std::strlen(asName);
        const std::size_t lSuffixLength = std::strlen(asSuffix);
        return lNameLength >= lSuffixLength &&
            std::strcmp(asName + lNameLength - lSuffixLength, asSuffix) == 0;
    }

    inline const char* GetVariableType(const char* asName)
    {
        if(std::strcmp(asName, "Body_AccurateClimbing") == 0) return "Bool";
        if(std::strcmp(asName, "Toughness") == 0 ||
           std::strcmp(asName, "NormalAttackStrength") == 0 ||
           std::strcmp(asName, "BreakDoorAttackStrength") == 0) return "Int";
        if(std::strcmp(asName, "Body_Size") == 0 ||
           std::strcmp(asName, "Body_OffsetRot") == 0 ||
           std::strcmp(asName, "Body_OffsetTrans") == 0 ||
           std::strcmp(asName, "NormalDamageSize") == 0 ||
           std::strcmp(asName, "NormalDamageOffset") == 0) return "Vector3";
        if(std::strcmp(asName, "NormalAttackDamage") == 0 ||
           std::strcmp(asName, "NormalAttackForce") == 0 ||
           std::strcmp(asName, "BreakDoorAttackDamage") == 0 ||
           std::strcmp(asName, "BreakDoorAttackForce") == 0) return "Vector2";
        if(StartsWith(asName, "HitPS") || StartsWith(asName, "HitSound") ||
           StartsWith(asName, "Llama") || EndsWith(asName, "Sound") ||
           EndsWith(asName, "DamageType")) return "String";
        return "Float";
    }

    inline int GetVariableGroup(const char* asName)
    {
        if(StartsWith(asName, "Body_")) return 0;
        if(StartsWith(asName, "Walk_") || StartsWith(asName, "Run_") ||
           StartsWith(asName, "Turn") || StartsWith(asName, "StoppedTo") ||
           StartsWith(asName, "WalkTo") || StartsWith(asName, "RunTo") ||
           StartsWith(asName, "MoveSpeed") || StartsWith(asName, "WaterStep")) return 1;
        if(StartsWith(asName, "Llama")) return 3;
        return 2;
    }

    inline void AddVariable(hpl::cXmlElement* apParent, const char* asName,
                            const char* asType, const char* asDefault,
                            const char* asDescription = "")
    {
        hpl::cXmlElement* pVar = apParent->CreateChildElement("Var");
        pVar->SetAttributeString("Name", asName);
        pVar->SetAttributeString("Type", asType);
        pVar->SetAttributeString("DefaultValue", asDefault);
        if(asDescription[0] != '\0') pVar->SetAttributeString("Description", asDescription);
    }
}

// Append a self-contained type without changing legacy type indices or a custom
// Enemy_Llama definition supplied by the game/editor configuration.
inline void AddEnemyLlamaEditorDefinition(hpl::cXmlElement* apTypes)
{
    if(apTypes == NULL) return;
    hpl::cXmlNodeListIterator it = apTypes->GetChildIterator();
    while(it.HasNext())
    {
        hpl::cXmlElement* pType = it.Next()->ToElement();
        if(pType && pType->GetValue() == "Type" &&
           pType->GetAttributeString("Name") == "Enemy_Llama") return;
    }

    hpl::cXmlElement* pType = apTypes->CreateChildElement("Type");
    pType->SetAttributeString("Name", "Enemy_Llama");
    pType->SetAttributeString("Default", "Grunt");

    hpl::cXmlElement* pTypeVars = pType->CreateChildElement("TypeVars");
    hpl::cXmlElement* pObservation = pTypeVars->CreateChildElement("Group");
    pObservation->SetAttributeString("Name", "Llama observation");
    using EditorEnemyLlamaDefinition::AddVariable;
    AddVariable(pObservation, "FOV", "Float", "120", "Horizontal observation field of view in degrees. Runtime clamps to 1-179 degrees; non-finite values use 120.");
    AddVariable(pObservation, "LlamaObservationWidth", "Int", "1280", "Observation image width in pixels. Runtime clamps to 64-2048 pixels.");
    AddVariable(pObservation, "LlamaObservationHeight", "Int", "864", "Observation image height in pixels. Runtime clamps to 64-2048 pixels.");
    AddVariable(pObservation, "LlamaObservationInterval", "Float", "0.25", "Seconds between observation captures.");
    AddVariable(pObservation, "LlamaCameraOffset", "Vector3", "0 -0.1 0", "Camera offset relative to the top of the character cylinder.");
    AddVariable(pObservation, "LlamaHearingRange", "Float", "12", "Maximum perceived sound distance in game units.");
    AddVariable(pObservation, "LlamaSoundThreshold", "Float", "0.2", "Minimum perceived sound loudness.");

    hpl::cXmlElement* pControl = pTypeVars->CreateChildElement("Group");
    pControl->SetAttributeString("Name", "Llama control");
    AddVariable(pControl, "LlamaControlEnabled", "Bool", "true", "Allow the shared local vision model to control this enemy.");
    AddVariable(pControl, "LlamaDecisionInterval", "Float", "1.0", "Minimum seconds between model decisions. Runtime clamps to 0.1-10 seconds.");
    AddVariable(pControl, "LlamaActionMaxSeconds", "Float", "1.5", "Maximum duration of a model movement action. Runtime clamps to 0.1-2 seconds.");
    AddVariable(pControl, "LlamaAttackAnimation", "String", "", "Optional normal attack animation name; empty selects an available conventional name.");
    AddVariable(pControl, "LlamaDoorAttackAnimation", "String", "", "Optional break-door animation name; empty selects an available conventional name.");
    AddVariable(pControl, "LlamaAttackCooldown", "Float", "1.0", "Minimum seconds between mechanical attacks. Runtime clamps to 0.2-10 seconds.");

    hpl::cXmlElement* pInstanceVars = pType->CreateChildElement("InstanceVars");
    AddVariable(pInstanceVars, "CallbackFunc", "String", "", "Standard enemy callback function.");
    AddVariable(pInstanceVars, "DisableTriggers", "Bool", "false", "Disable perceived sound events.");

    hpl::cXmlElement* pSubTypes = pType->CreateChildElement("SubTypes");
    const char* vGroupNames[] = { "Body", "Movement", "Health and attacks", "Animation names" };
    for(std::size_t i = 0; i < kLuxLlamaProfileCount; ++i)
    {
        const cLuxLlamaProfile& profile = kLuxLlamaProfiles[i];
        hpl::cXmlElement* pSubType = pSubTypes->CreateChildElement("SubType");
        pSubType->SetAttributeString("Name", profile.msName);
        hpl::cXmlElement* pVars = pSubType->CreateChildElement("TypeVars");
        hpl::cXmlElement* vGroups[4];
        for(int j = 0; j < 4; ++j)
        {
            vGroups[j] = pVars->CreateChildElement("Group");
            vGroups[j]->SetAttributeString("Name", vGroupNames[j]);
        }
        for(std::size_t j = 0; j < profile.mlVariableCount; ++j)
        {
            const cLuxLlamaProfileVariable& variable = profile.mpVariables[j];
            AddVariable(vGroups[EditorEnemyLlamaDefinition::GetVariableGroup(variable.msName)],
                        variable.msName, EditorEnemyLlamaDefinition::GetVariableType(variable.msName),
                        variable.msValue);
        }
    }
}

#endif // HPL_EDITOR_ENEMY_LLAMA_DEFINITION_H
