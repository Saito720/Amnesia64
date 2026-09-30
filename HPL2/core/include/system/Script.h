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

#ifndef HPL_SCRIPT_H
#define HPL_SCRIPT_H

#include "resources/ResourceBase.h"
#include <vector>

#ifdef __GNUC__
	#if defined __ppc__ || defined(__LP64__)
		#define __stdcall
	#else
		#define __stdcall __attribute__((stdcall))
	#endif
#endif

namespace hpl {
	// True only while the local scripting VM is executing a script context.
	bool IsScriptExecuting();
	// Opaque identity of the currently executing VM context, or NULL outside
	// script execution. A script callback may enter a second context while a
	// native binding from the first context remains on the call stack.
	const void* GetActiveScriptContext();

	class iScript : public iResourceBase
	{
	public:
		iScript(const tString& asName, const tWString& asFullPath) : iResourceBase(asName, asFullPath,0){}
		virtual ~iScript(){}

		bool Reload(){ return false;}
		void Unload(){}
		void Destroy(){}
		
		virtual bool CreateFromFile(const tWString& asFile, tString *apCompileMessages=NULL,
			unsigned alMaxInitializationLineCallbacks=0)=0;
		
		virtual int GetFuncHandle(const tString& asFunc)=0;
		
		virtual void AddArg(const tString& asArg)=0;
		
		/**
		 * Runs a func in the script, for example "test(15)"
		 * \param asFuncLine the line of code
		 * \return true if everything was ok, else false
		 */
		virtual bool Run(const tString& asFuncLine)=0;
		// Budgeted text dispatch for trusted authority callback expressions.
		// A nested call shares its caller's remaining execution budget.
		virtual bool Run(const tString& asFuncLine, tString *apError, unsigned alMaxLineCallbacks)=0;
		
		virtual bool Run(int alHandle)=0;
		// Source stays in memory. Deferred globals are initialized explicitly once
		// the owning runtime and its world state are ready.
		virtual bool CreateFromSource(const tString& asSection, const tString& asSource,
			tString *apCompileMessages=NULL, bool abInitializeGlobals=true,
			unsigned alMaxInitializationLineCallbacks=0)=0;
		virtual bool InitializeGlobals(tString *apError=NULL, unsigned alMaxLineCallbacks=0)=0;
		virtual int GetFuncHandleByDecl(const tString& asDecl)=0;
		virtual bool HasFunctionNamed(const tString& asName)=0;
		// Calls either string arguments (including none) or one float argument.
		// Each call has an independent VM context, including nested callbacks.
		// A zero budget preserves unlimited top-level legacy execution; nested
		// calls inherit any active budget and bounded nesting depth.
		virtual bool RunTyped(int alHandle, const std::vector<tString>& avStringArgs,
			const float *apFloatArg=NULL, tString *apError=NULL, unsigned alMaxLineCallbacks=0)=0;
		// Strict void(int) dispatch for actor IDs; uses the same context/budget rules.
		virtual bool RunTypedInt(int alHandle, int alValue, tString *apError=NULL,
			unsigned alMaxLineCallbacks=0)=0;
	};
};
#endif // HPL_SCRIPT_H
