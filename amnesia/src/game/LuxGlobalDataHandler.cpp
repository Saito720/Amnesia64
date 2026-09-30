/*
 * Copyright © 2009-2020 Frictional Games
 * 
 * This file is part of Amnesia: The Dark Descent.
 * 
 * Amnesia: The Dark Descent is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. 

 * Amnesia: The Dark Descent is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Amnesia: The Dark Descent.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "LuxGlobalDataHandler.h"
#include "LuxScriptHandler.h"
#include "LuxScriptRuntime.h"
#include "LuxMultiplayer.h"

#include "LuxMap.h"
#include "LuxMapHandler.h"
#include "LuxPlayer.h"
#include "LuxEnemy.h"


//////////////////////////////////////////////////////////////////////////
// CONSTRUCTORS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

cLuxGlobalDataHandler::cLuxGlobalDataHandler() : iLuxUpdateable("LuxGlobalDataHandler")
{
	mpScript = NULL;
	
	mfLightLampMinSanityIncrease = gpBase->mpGameCfg->GetFloat("Player_Sanity", "LightLampMinSanityIncrease",0);
	mfLightLampMaxSanityIncrease = gpBase->mpGameCfg->GetFloat("Player_Sanity", "LightLampMaxSanityIncrease",0);
}

//-----------------------------------------------------------------------

cLuxGlobalDataHandler::~cLuxGlobalDataHandler()
{
	if(mpScript)
	{
		gpBase->mpEngine->GetResources()->GetScriptManager()->Destroy(mpScript);
	}
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------

void cLuxGlobalDataHandler::LoadAndInitGlobalScript()
{
	if(gpBase->mpMultiplayer && gpBase->mpMultiplayer->IsClient()) return;
	LoadScript();

	RunScript("OnGameStart()");
}

//-----------------------------------------------------------------------

void cLuxGlobalDataHandler::OnGameStart()
{
	
}

//-----------------------------------------------------------------------

void cLuxGlobalDataHandler::OnStart()
{
	
}

//-----------------------------------------------------------------------

void cLuxGlobalDataHandler::Reset()
{
	m_mapVars.clear();

	if(mpScript) gpBase->mpEngine->GetResources()->GetScriptManager()->Destroy(mpScript);
	mpScript = NULL;

	mfEnemyActivateSoundCount =0;
}

//-----------------------------------------------------------------------

void cLuxGlobalDataHandler::Update(float afTimeStep)
{
	if(mfEnemyActivateSoundCount>0)
	{
		mfEnemyActivateSoundCount-=afTimeStep;
	}
}

//-----------------------------------------------------------------------

void cLuxGlobalDataHandler::OnMapEnter(cLuxMap *apMap)
{
}

//-----------------------------------------------------------------------

void cLuxGlobalDataHandler::OnMapLeave(cLuxMap *apMap)
{
}


//-----------------------------------------------------------------------

void cLuxGlobalDataHandler::LoadScript()
{
    msScriptLoadError.clear();
	/////////////////////
	// Destroy old
	if(mpScript)
	{
		gpBase->mpEngine->GetResources()->GetScriptManager()->Destroy(mpScript);
		mpScript = NULL;
	}

	/////////////////////
	// Load script
	tString sFile = gpBase->mpMapHandler->GetMapFolder() + "global.hps";
	cLuxScriptRuntime* runtime=gpBase->mpScriptHandler->GetRuntime();
    cLuxScriptAuthorityInitializationScope initialization(runtime->IsRevised(),"global",
        gpBase->mpMultiplayer && gpBase->mpMultiplayer->IsActive()?gpBase->mpMultiplayer->GetSessionSerial():0,
        gpBase->mpMultiplayer && gpBase->mpMultiplayer->IsActive()?gpBase->mpMultiplayer->GetMapEpoch():0);
    tString compileMessages;
    mpScript = gpBase->mpEngine->GetResources()->GetScriptManager()->CreateScript(sFile,&compileMessages,
        runtime->IsRevised()?cLuxScriptRuntime::ExecutionLineBudget:0);
    if(!mpScript && runtime->IsRevised() && cPlatform::FileExists(cString::To16Char(sFile)))
        msScriptLoadError="Could not load authority module "+sFile+": "+compileMessages;
    tString error;if(mpScript && !runtime->ValidateAuthorityScript(mpScript,"global",error)) {
        msScriptLoadError=error;
        Error("Invalid authority script: %.4096s\n",error.c_str());
        gpBase->mpEngine->GetResources()->GetScriptManager()->Destroy(mpScript);mpScript=NULL;
    }
	if(mpScript==NULL)
	{
		Error("Global script '%s' could not be created!\n", sFile.c_str());
	}
}

bool cLuxGlobalDataHandler::RecompileScript(tString *apOutput)
{
	if(mpScript)
		gpBase->mpEngine->GetResources()->GetScriptManager()->Destroy(mpScript);

	tString sFile = gpBase->mpMapHandler->GetMapFolder() + "global.hps";
	cLuxScriptRuntime* runtime=gpBase->mpScriptHandler->GetRuntime();
    cLuxScriptAuthorityInitializationScope initialization(runtime->IsRevised(),"global",
        gpBase->mpMultiplayer && gpBase->mpMultiplayer->IsActive()?gpBase->mpMultiplayer->GetSessionSerial():0,
        gpBase->mpMultiplayer && gpBase->mpMultiplayer->IsActive()?gpBase->mpMultiplayer->GetMapEpoch():0);
    mpScript = gpBase->mpEngine->GetResources()->GetScriptManager()->CreateScript(sFile, apOutput,
        runtime->IsRevised()?cLuxScriptRuntime::ExecutionLineBudget:0);
    tString error;if(mpScript && !runtime->ValidateAuthorityScript(mpScript,"global",error)) {
        if(apOutput) *apOutput+=error;
        gpBase->mpEngine->GetResources()->GetScriptManager()->Destroy(mpScript);mpScript=NULL;
    }

	return mpScript != NULL;
}

//-----------------------------------------------------------------------

void cLuxGlobalDataHandler::OnScriptPlayerReady(uint32_t peer)
{
    tString error;
    if(!gpBase->mpScriptHandler->GetRuntime()->RunAuthorityPlayerReady(mpScript,"global",peer,error))
        Error("Player ready callback: %.4096s\n",error.c_str());
}

void cLuxGlobalDataHandler::RunScript(const tString& asCommand)
{
	if(gpBase->mpMultiplayer && gpBase->mpMultiplayer->IsClient()) return;
	if(mpScript==NULL) return;

    tString error;
    cLuxScriptRuntime* runtime=gpBase->mpScriptHandler->GetRuntime();
    auto context=LuxCurrentScriptContext();context.module="global";cLuxScriptExecutionScope scope(context);
    const bool ok=asCommand=="OnGameStart()"?runtime->RunAuthorityHook(mpScript,"OnGameStart",error,NULL,"global"):
        runtime->RunAuthorityCommand(mpScript,asCommand,error);
    if(!ok) Error("Global script: %s\n",error.c_str());
}

//-----------------------------------------------------------------------

cLuxScriptVar* cLuxGlobalDataHandler::GetVar(const tString &asName)
{
	tLuxScriptVarMapIt it = m_mapVars.find(asName);
	if(it != m_mapVars.end()) return &(it->second);

	m_mapVars.insert(tLuxScriptVarMap::value_type(asName, cLuxScriptVar(asName)));
	it = m_mapVars.find(asName);
	return &(it->second);
}

//-----------------------------------------------------------------------

bool cLuxGlobalDataHandler::GetEnemyActivateSoundAllowed()
{
	return mfEnemyActivateSoundCount<=0;
}

void cLuxGlobalDataHandler::SetEnemyActivateSoundMade()
{
	mfEnemyActivateSoundCount = 5.0f;
}

//-----------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
// PRIVATE METHODS
//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------


//-----------------------------------------------------------------------

