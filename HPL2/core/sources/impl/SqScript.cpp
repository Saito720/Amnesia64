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

#include "impl/SqScript.h"
#include "system/LowLevelSystem.h"
#include "system/String.h"
#include "system/Platform.h"
#include "math/Math.h"
#include <stdio.h>
#include <limits>
#include "impl/scripthelper.h"
#include "impl/scriptstring.h"
#include "resources/BinaryBuffer.h"
#include "resources/Resources.h"

namespace hpl {
	bool IsScriptExecuting() { return asGetActiveContext() != NULL; }
	const void* GetActiveScriptContext() { return asGetActiveContext(); }

	namespace {
		bool ScriptFailure(const tString& asMessage, tString *apError)
		{
			if(apError) *apError = asMessage;
			else Error("%s\n", asMessage.c_str());
			return false;
		}

		struct cScriptCallResources
		{
			asIScriptContext *mpContext;
			std::vector<CScriptString*> mvStrings;
			explicit cScriptCallResources(asIScriptEngine *apEngine)
				: mpContext(apEngine->CreateContext()) {}
			~cScriptCallResources()
			{
				// Reference arguments must outlive the prepared context's stack.
				if(mpContext) mpContext->Release();
				for(size_t i=0; i<mvStrings.size(); ++i) mvStrings[i]->Release();
			}
		};

		struct cScriptLineBudget
		{
			unsigned mlRemaining;
			bool mbExceeded;
			bool mbLimited;
			int mlLine;
			tString msFunctionDeclaration, msSection;
			cScriptLineBudget *mpPrevious;
			static cScriptLineBudget*& Current()
			{
				static thread_local cScriptLineBudget *pCurrent=NULL;
				return pCurrent;
			}
			explicit cScriptLineBudget(unsigned alLimit)
				: mlRemaining(alLimit), mbExceeded(false), mbLimited(alLimit!=0), mlLine(0), mpPrevious(Current())
			{
				if(mbLimited || mpPrevious) Current()=this;
			}
			~cScriptLineBudget() { if(Current()==this) Current()=mpPrevious; }
			bool Enabled() const { return mbLimited || mpPrevious; }
			static bool NestingExceeded()
			{
				unsigned lDepth=1;
				for(cScriptLineBudget *pBudget=Current();pBudget;pBudget=pBudget->mpPrevious)
					if(++lDepth>64) return true;
				return false;
			}
			static void Check(asIScriptContext *apContext, void *apData)
			{
				cScriptLineBudget *pBudget = static_cast<cScriptLineBudget*>(apData);
				for(cScriptLineBudget *pCurrent=pBudget;pCurrent;pCurrent=pCurrent->mpPrevious)
				{
					if(!pCurrent->mbLimited) continue;
					if(pCurrent->mlRemaining) --pCurrent->mlRemaining;
					else {
						// An ancestor may belong to another engine, and a temporary
						// ExecuteString function may retire before that ancestor returns.
						if(!pCurrent->mbExceeded) {
							pCurrent->mlLine=apContext->GetCurrentLineNumber();
							asIScriptFunction *pFunction=apContext->GetEngine()->GetFunctionDescriptorById(apContext->GetCurrentFunction());
							if(pFunction) {
								pCurrent->msFunctionDeclaration=pFunction->GetDeclaration();
								const char *pSection=pFunction->GetScriptSectionName();
								pCurrent->msSection=pSection?pSection:"";
							}
						}
						pCurrent->mbExceeded=true;pBudget->mbExceeded=true;
						pBudget->mlLine=pCurrent->mlLine;pBudget->msFunctionDeclaration=pCurrent->msFunctionDeclaration;pBudget->msSection=pCurrent->msSection;
						apContext->Abort();return;
					}
				}
			}
		};

		tString ScriptExecutionError(asIScriptContext *apContext, int alResult, const cScriptLineBudget *apBudget=NULL)
		{
			const bool abBudgetExceeded=apBudget && apBudget->mbExceeded;
			tString sError = abBudgetExceeded ? "Script execution budget exceeded" : "Script execution failed";
			if(abBudgetExceeded && !apBudget->msFunctionDeclaration.empty()) {
				sError+=" in "+apBudget->msFunctionDeclaration;
				if(!apBudget->msSection.empty()) sError+=" ("+apBudget->msSection+":"+cString::ToString(apBudget->mlLine)+")";
				return sError;
			}
			int lFunction = apContext->GetCurrentFunction();
			int lLine = apContext->GetCurrentLineNumber();
			if(alResult == asEXECUTION_EXCEPTION)
			{
				lFunction = apContext->GetExceptionFunction();
				lLine = apContext->GetExceptionLineNumber();
				const char *pException = apContext->GetExceptionString();
				if(pException) sError += ": " + tString(pException);
			}
			else if(!abBudgetExceeded) sError += " (status " + cString::ToString(alResult) + ")";
			asIScriptFunction *pFunction = apContext->GetEngine()->GetFunctionDescriptorById(lFunction);
			if(pFunction)
			{
				sError += " in " + tString(pFunction->GetDeclaration());
				const char *pSection = pFunction->GetScriptSectionName();
				if(pSection) sError += " (" + tString(pSection) + ":" + cString::ToString(lLine) + ")";
			}
			return sError;
		}

		struct cScriptGlobalInitSetting
		{
			asIScriptEngine *mpEngine;
			asPWORD mlPrevious;
			cScriptGlobalInitSetting(asIScriptEngine *apEngine, bool abEnabled)
				: mpEngine(apEngine), mlPrevious(apEngine->GetEngineProperty(asEP_INIT_GLOBAL_VARS_AFTER_BUILD))
			{
				mpEngine->SetEngineProperty(asEP_INIT_GLOBAL_VARS_AFTER_BUILD, abEnabled ? 1 : 0);
			}
			~cScriptGlobalInitSetting()
			{
				mpEngine->SetEngineProperty(asEP_INIT_GLOBAL_VARS_AFTER_BUILD, mlPrevious);
			}
		};
		struct cScriptInitializingScope
		{
			bool& active;
			explicit cScriptInitializingScope(bool& value) : active(value) {active=true;}
			~cScriptInitializingScope() {active=false;}
		};
	}

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC DATA
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	#define kEncryptKey 0x4516FFDD

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cSqScript::cSqScript(const tString& asName,asIScriptEngine *apScriptEngine,
							cScriptOutput *apScriptOutput, int alHandle)
		: iScript(asName, _W(""))
	{
		mpScriptEngine = apScriptEngine;
		mpScriptOutput = apScriptOutput;
		mlHandle = alHandle;

		mpModule = NULL;
		mbGlobalsInitialized = false;
		mbGlobalsInitializing = false;

		//Create a unique module name
		msModuleName = "Module_"+cString::ToString(cMath::RandRectl(0,1000000))+
						"_"+cString::ToString(mlHandle);

	}

	cSqScript::~cSqScript()
	{
		mpScriptEngine->DiscardModule(msModuleName.c_str());
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	bool cSqScript::CreateFromFile(const tWString& asFileName, tString *apCompileMessages,
		unsigned alMaxInitializationLineCallbacks)
	{
		SetFullPath(asFileName);

		tWString sExt = cString::ToLowerCaseW(cString::GetFileExtW(asFileName));

		/////////////////////////////////////////
		// Load file
		int lLength = 0;
		char *pCharBuffer = NULL;
		
		/////////////////////////////////////
		// Normal load
		if(sExt == _W("hps"))
		{
			pCharBuffer = LoadCharBuffer(asFileName,lLength);
			if(pCharBuffer==NULL){
				Error("Couldn't load script '%s'!\n",asFileName.c_str());
				return false;
			}
		}
		/////////////////////////////////////
		// Compressed load
		else if(cResources::GetCreateAndLoadCompressedMaps())
		{
			cBinaryBuffer compBuffer;
			if(compBuffer.Load(asFileName)==false)
			{
				//Log("Could not load compressed map!\n");
				return false;
			}

			int lKey = kEncryptKey;
			compBuffer.XorTransform((char*)&lKey, sizeof(lKey));

			cBinaryBuffer textBuff;
			if(textBuff.DecompressAndAddFromBuffer(&compBuffer, false)==false)
			{
				//Log("Could not decompress map!\n");
				return false;
			}

			textBuff.SetPos(0);
			lLength = (int)textBuff.GetSize();
			pCharBuffer = hplNewArray(char,lLength);
			textBuff.GetCharArray(pCharBuffer, lLength);
		}

		/////////////////////////////////////////
		// Save compressed
		if(sExt == _W("hps") && cResources::GetCreateAndLoadCompressedMaps())
		{
			tWString sCompFile = cString::SetFileExtW(asFileName,_W("chps"));

			//Only recreate if file does not exist or if out of date.
			if(	cPlatform::FileExists(sCompFile)==false || 
				cPlatform::FileModifiedDate(sCompFile) < cPlatform::FileModifiedDate(asFileName))
			{
				cBinaryBuffer textBuff;
				textBuff.AddCharArray(pCharBuffer, lLength);

				cBinaryBuffer compBuff;
				compBuff.CompressAndAdd(textBuff.GetDataPointer(), textBuff.GetSize());

				int lKey = kEncryptKey;
				compBuff.XorTransform((char*)&lKey, sizeof(lKey));

				compBuff.Save(sCompFile);
			}
		}
		
		if(pCharBuffer==NULL) return ScriptFailure("Unsupported script file format", apCompileMessages);
		const bool bCreated = CreateFromSource(cString::To8Char(asFileName),
			tString(pCharBuffer, lLength), apCompileMessages, true, alMaxInitializationLineCallbacks);
		hplDeleteArray(pCharBuffer);
		return bCreated;
	}

	bool cSqScript::CreateFromSource(const tString& asSection, const tString& asSource,
		tString *apCompileMessages, bool abInitializeGlobals, unsigned alMaxInitializationLineCallbacks)
	{
		if(mbGlobalsInitializing) return ScriptFailure("Cannot rebuild a script during global initialization", apCompileMessages);
		if(apCompileMessages) apCompileMessages->clear();
		mpScriptOutput->Clear();
		mbGlobalsInitialized = false;
		mpModule = mpScriptEngine->GetModule(msModuleName.c_str(), asGM_ALWAYS_CREATE);
		if(!mpModule) return ScriptFailure("Could not create script module for " + asSection, apCompileMessages);
		const int lSectionResult = mpModule->AddScriptSection(asSection.c_str(), asSource.data(), asSource.size());
		if(lSectionResult<0)
		{
			mpModule = NULL;
			return ScriptFailure("Could not add script section " + asSection + " (status " + cString::ToString(lSectionResult) + ")", apCompileMessages);
		}

		int lBuildResult;
		{
			// The property belongs to the engine, so never leave a client's
			// deferred-initialization choice applied to a later legacy build.
			cScriptGlobalInitSetting setting(mpScriptEngine, abInitializeGlobals && !alMaxInitializationLineCallbacks);
			cScriptInitializingScope initializing(mbGlobalsInitializing);
			lBuildResult = mpModule->Build();
		}
		tString sMessages = mpScriptOutput->GetMessage();
		mpScriptOutput->Clear();
		if(apCompileMessages) *apCompileMessages = sMessages;
		if(lBuildResult<0)
		{
			mpModule = NULL;
			if(sMessages.empty()) sMessages = "Could not build script " + asSection + " (status " + cString::ToString(lBuildResult) + ")";
			return ScriptFailure(sMessages, apCompileMessages);
		}
		mbGlobalsInitialized = abInitializeGlobals && !alMaxInitializationLineCallbacks;
		if(abInitializeGlobals && alMaxInitializationLineCallbacks)
		{
			tString sInitializationError;
			if(!InitializeGlobals(&sInitializationError, alMaxInitializationLineCallbacks))
			{
				// Preserve build warnings alongside the failing initializer.
				return ScriptFailure(sMessages+sInitializationError,apCompileMessages);
			}
		}
		return true;
	}

	bool cSqScript::InitializeGlobals(tString *apError, unsigned alMaxLineCallbacks)
	{
		if(apError) apError->clear();
		if(!mpModule) return ScriptFailure("Cannot initialize globals without a script module", apError);
		if(mbGlobalsInitialized) return true;
		if(mbGlobalsInitializing) return ScriptFailure("Script global initialization is already active", apError);
		if(cScriptLineBudget::NestingExceeded())
			return ScriptFailure("Script execution nesting limit exceeded", apError);
		cScriptCallResources call(mpScriptEngine);
		if(!call.mpContext) return ScriptFailure("Could not create script initialization context", apError);
		cScriptLineBudget budget(alMaxLineCallbacks);
		if(budget.Enabled() && call.mpContext->SetLineCallback(asFUNCTION(cScriptLineBudget::Check), &budget, asCALL_CDECL)<0)
			return ScriptFailure("Could not install script initialization budget", apError);
		mpScriptOutput->Clear();
		cScriptInitializingScope initializing(mbGlobalsInitializing);
		const int lResult = mpModule->ResetGlobalVars(call.mpContext);
		tString sMessages = mpScriptOutput->GetMessage();
		mpScriptOutput->Clear();
		if(lResult<0 || budget.mbExceeded)
		{
			if(budget.mbExceeded) sMessages = ScriptExecutionError(call.mpContext, call.mpContext->GetState(), &budget);
			if(sMessages.empty()) sMessages = "Could not initialize script globals (status " + cString::ToString(lResult) + ")";
			return ScriptFailure(sMessages, apError);
		}
		mbGlobalsInitialized = true;
		return true;
	}

	//-----------------------------------------------------------------------

	int cSqScript::GetFuncHandle(const tString& asFunc)
	{
		return mpModule ? mpModule->GetFunctionIdByName(asFunc.c_str()) : asNO_FUNCTION;
	}

	int cSqScript::GetFuncHandleByDecl(const tString& asDecl)
	{
		return mpModule ? mpModule->GetFunctionIdByDecl(asDecl.c_str()) : asNO_FUNCTION;
	}

	bool cSqScript::HasFunctionNamed(const tString& asName)
	{
		if(!mpModule) return false;
		for(int i=0; i<mpModule->GetFunctionCount(); ++i)
		{
			asIScriptFunction *pFunction = mpModule->GetFunctionDescriptorByIndex(i);
			if(pFunction && asName == pFunction->GetName()) return true;
		}
		return false;
	}

	bool cSqScript::HasScriptDefinedObjectTypes() const
	{
		if(!mpModule) return false;
		for(int i=0; i<mpModule->GetObjectTypeCount(); ++i)
		{
			asIObjectType *pType = mpModule->GetObjectTypeByIndex(i);
			if(pType && (pType->GetFlags() & asOBJ_SCRIPT_OBJECT)) return true;
		}
		return false;
	}

	//-----------------------------------------------------------------------

	void cSqScript::AddArg(const tString& asArg)
	{

	}

	//-----------------------------------------------------------------------

	bool cSqScript::Run(const tString& asFuncLine)
	{
		return Run(asFuncLine,NULL,0);
	}

	bool cSqScript::Run(const tString& asFuncLine, tString *apError, unsigned alMaxLineCallbacks)
	{
		if(apError) apError->clear();
		if(!mpModule || !mbGlobalsInitialized) return ScriptFailure("Script module is not initialized", apError);
		if(cScriptLineBudget::NestingExceeded())
			return ScriptFailure("Script execution nesting limit exceeded", apError);
		cScriptCallResources call(mpScriptEngine);
		if(!call.mpContext) return ScriptFailure("Could not create script execution context", apError);
		cScriptLineBudget budget(alMaxLineCallbacks);
		if(budget.Enabled() && call.mpContext->SetLineCallback(asFUNCTION(cScriptLineBudget::Check), &budget, asCALL_CDECL)<0)
			return ScriptFailure("Could not install script execution budget", apError);
		mpScriptOutput->Clear();
		const int lResult = ExecuteString(mpScriptEngine, asFuncLine.c_str(), mpModule, call.mpContext);
		tString sMessages = mpScriptOutput->GetMessage();
		mpScriptOutput->Clear();
		if(lResult == asEXECUTION_FINISHED && !budget.mbExceeded) return true;
		if(budget.mbExceeded || sMessages.empty()) sMessages = ScriptExecutionError(call.mpContext, lResult, &budget);
		return ScriptFailure(sMessages, apError);
	}

	//-----------------------------------------------------------------------

	bool cSqScript::Run(int alHandle)
	{
		return RunTyped(alHandle, std::vector<tString>());
	}

	bool cSqScript::RunTyped(int alHandle, const std::vector<tString>& avStringArgs,
		const float *apFloatArg, tString *apError, unsigned alMaxLineCallbacks)
	{
		return RunTypedInternal(alHandle,avStringArgs,apFloatArg,NULL,apError,alMaxLineCallbacks);
	}

	bool cSqScript::RunTypedInt(int alHandle, int alValue, tString *apError, unsigned alMaxLineCallbacks)
	{
		return RunTypedInternal(alHandle,std::vector<tString>(),NULL,&alValue,apError,alMaxLineCallbacks);
	}

	bool cSqScript::RunTypedInternal(int alHandle, const std::vector<tString>& avStringArgs,
		const float *apFloatArg, const int *apIntArg, tString *apError, unsigned alMaxLineCallbacks)
	{
		if(apError) apError->clear();
		if(!mpModule || !mbGlobalsInitialized) return ScriptFailure("Script module is not initialized", apError);
		if(cScriptLineBudget::NestingExceeded())
			return ScriptFailure("Script execution nesting limit exceeded", apError);
		asIScriptFunction *pFunction = alHandle<0 ? NULL : mpModule->GetFunctionDescriptorById(alHandle);
		if(!pFunction || !pFunction->GetModuleName() || msModuleName != pFunction->GetModuleName() || pFunction->IsClassMethod())
			return ScriptFailure("Invalid function handle for this script module", apError);
		if(apFloatArg && !avStringArgs.empty()) return ScriptFailure("Typed script calls cannot mix float and string arguments", apError);
		if(apIntArg && pFunction->GetReturnTypeId()!=asTYPEID_VOID)
			return ScriptFailure("Integer script calls require a void return type", apError);
		const size_t lArgCount = (apFloatArg || apIntArg) ? 1 : avStringArgs.size();
		if(static_cast<size_t>(pFunction->GetParamCount()) != lArgCount)
			return ScriptFailure("Incorrect argument count for " + tString(pFunction->GetDeclaration()), apError);
		const int lExpectedType = apFloatArg ? asTYPEID_FLOAT : apIntArg ? asTYPEID_INT32 : mpScriptEngine->GetTypeIdByDecl("string");
		for(size_t i=0; i<lArgCount; ++i)
		{
			asDWORD lFlags=0;
			const int lType = pFunction->GetParamTypeId(static_cast<int>(i), &lFlags);
			if(lType != lExpectedType || ((apFloatArg || apIntArg) ? lFlags != asTM_NONE : (lFlags != asTM_NONE && lFlags != asTM_INREF)))
				return ScriptFailure("Unsupported argument type for " + tString(pFunction->GetDeclaration()), apError);
		}

		cScriptCallResources call(mpScriptEngine);
		if(!call.mpContext) return ScriptFailure("Could not create script execution context", apError);
		int lResult = call.mpContext->Prepare(alHandle);
		if(lResult<0) return ScriptFailure("Could not prepare " + tString(pFunction->GetDeclaration()) + " (status " + cString::ToString(lResult) + ")", apError);
		if(apFloatArg) lResult = call.mpContext->SetArgFloat(0, *apFloatArg);
		else if(apIntArg) lResult = call.mpContext->SetArgDWord(0, static_cast<asDWORD>(*apIntArg));
		else
		{
			for(size_t i=0; i<avStringArgs.size(); ++i)
			{
				CScriptString *pString = new CScriptString(avStringArgs[i]);
				call.mvStrings.push_back(pString);
				lResult = call.mpContext->SetArgObject(static_cast<asUINT>(i), pString);
				if(lResult<0) break;
			}
		}
		if(lResult<0) return ScriptFailure("Could not set arguments for " + tString(pFunction->GetDeclaration()), apError);
		cScriptLineBudget budget(alMaxLineCallbacks);
		if(budget.Enabled() && call.mpContext->SetLineCallback(asFUNCTION(cScriptLineBudget::Check), &budget, asCALL_CDECL)<0)
			return ScriptFailure("Could not install script execution budget", apError);
		lResult = call.mpContext->Execute();
		if(lResult != asEXECUTION_FINISHED || budget.mbExceeded) return ScriptFailure(ScriptExecutionError(call.mpContext, lResult, &budget), apError);
		return true;
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	char* cSqScript::LoadCharBuffer(const tWString& asFileName, int& alLength)
	{
		FILE *pFile = cPlatform::OpenFile(asFileName, _W("rb"));
		if(pFile==NULL){
			return NULL;
		}

		if(fseek(pFile,0,SEEK_END)) {fclose(pFile);return NULL;}
		const long lLength=ftell(pFile);
		if(lLength<0 || lLength>=std::numeric_limits<int>::max() || fseek(pFile,0,SEEK_SET))
		{ fclose(pFile);return NULL; }
		char *pBuffer = hplNewArray(char,static_cast<int>(lLength)+1);
		const bool bRead=lLength==0 || fread(pBuffer,1,lLength,pFile)==static_cast<size_t>(lLength);
		fclose(pFile);
		if(!bRead) {hplDeleteArray(pBuffer);return NULL;}
		pBuffer[lLength]=0;alLength=static_cast<int>(lLength);return pBuffer;
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// STATIC PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	//-----------------------------------------------------------------------

}
