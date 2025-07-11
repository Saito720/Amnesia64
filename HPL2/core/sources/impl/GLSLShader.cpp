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

#include "impl/GLSLShader.h"
#include "impl/SDLTexture.h"
#include "impl/LowLevelGraphicsSDL.h"
#include "system/LowLevelSystem.h"

#include "system/Platform.h"
#include "system/String.h"

#ifdef _WIN32
#include <io.h>
#endif

namespace hpl{

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cGLSLShader::cGLSLShader(const tString& asName,eGpuShaderType aType, iLowLevelGraphics *apLowLevelGraphics) 
				: iGpuShader(asName, _W(""), aType, eGpuProgramFormat_GLSL)
	{

		mpLowLevelGraphics = apLowLevelGraphics;
	}

	cGLSLShader::~cGLSLShader()
	{
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------
	
	bool cGLSLShader::Reload(){return false;}
	void cGLSLShader::Unload(){}
	void cGLSLShader::Destroy(){}
	
	//-----------------------------------------------------------------------

	bool cGLSLShader::CreateFromFile(const tWString &asFile, const tString &asEntry, bool abPrintInfoIfFail)
	{
		return true;
	}

	//-----------------------------------------------------------------------


	bool cGLSLShader::CreateFromString(const char *apStringData, const tString& asEntry, bool abPrintInfoIfFail)
	{
		return true;	
	}

	void cGLSLShader::LogShaderInfoLog()
	{
	}

	//-----------------------------------------------------------------------

	void cGLSLShader::LogShaderCode(const char *apStringData)
	{
	}

	//-----------------------------------------------------------------------

	GLenum cGLSLShader::GetGLShaderType(eGpuShaderType aType)
	{
		switch(aType)
		{
		case eGpuShaderType_Fragment:	return GL_FRAGMENT_SHADER;
		case eGpuShaderType_Vertex:		return GL_VERTEX_SHADER;
		}

		return GL_VERTEX_SHADER;
	}
	
	//-----------------------------------------------------------------------

}
