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

#include "impl/GLSLProgram.h"

#include "system/LowLevelSystem.h"
#include "system/String.h"

#include "impl/SDLTexture.h"
#include "impl/GLSLShader.h"
#include "impl/LowLevelGraphicsSDL.h"



namespace hpl{

	//-----------------------------------------------------------------------

	int cGLSLProgram::mlCurrentProgram =0;

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------
	
	cGLSLProgram::cGLSLProgram(const tString& asName) : iGpuProgram(asName,eGpuProgramFormat_GLSL)
	{

	}

	//-----------------------------------------------------------------------

	cGLSLProgram::~cGLSLProgram()
	{
	}
	
	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------
	
	bool cGLSLProgram::Link()
	{
		return true;
	}

	//-----------------------------------------------------------------------

	void cGLSLProgram::Bind()
	{
	}

	//-----------------------------------------------------------------------

	void cGLSLProgram::UnBind()
	{
	}


	//-----------------------------------------------------------------------

	bool cGLSLProgram::SetSamplerToUnit(const tString& asSamplerName, int alUnit)
	{
	return 0;
	}

	//-----------------------------------------------------------------------

	int cGLSLProgram::GetVariableId(const tString& asName)
	{
	return 0;
	}

	//-----------------------------------------------------------------------

	bool cGLSLProgram::GetVariableAsId(const tString& asName, int alId)
	{
		return true;
	}

	//-----------------------------------------------------------------------

	bool cGLSLProgram::SetInt(int alVarId, int alX)
	{
		return true;
	}

	//-----------------------------------------------------------------------


	bool  cGLSLProgram::SetFloat(int alVarId, float afX)
	{
		return true;
	}

	//-----------------------------------------------------------------------

	bool  cGLSLProgram::SetVec2f(int alVarId, float afX,float afY)
	{

		return true;
	}

	//-----------------------------------------------------------------------

	bool  cGLSLProgram::SetVec3f(int alVarId, float afX,float afY,float afZ)
	{
		return true;
	}

	//-----------------------------------------------------------------------

	bool  cGLSLProgram::SetVec4f(int alVarId, float afX,float afY,float afZ, float afW)
	{
		return true;
	}

	//-----------------------------------------------------------------------

	bool cGLSLProgram::SetMatrixf(int alVarId, const cMatrixf& aMtx)
	{
		return true;
	}

	//-----------------------------------------------------------------------

	bool cGLSLProgram::SetMatrixf(int alVarId, eGpuShaderMatrix aType, 
									eGpuShaderMatrixOp aOp)
	{
		return false;
	}

	
	void cGLSLProgram::LogProgramInfoLog()
	{
	}
	
	//-----------------------------------------------------------------------

}
