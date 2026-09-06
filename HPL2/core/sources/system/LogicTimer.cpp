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

#include "system/LogicTimer.h"

#include <chrono>
#include <cmath>

namespace hpl {

	//////////////////////////////////////////////////////////////////////////
	// CONSTRUCTORS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	cLogicTimer::cLogicTimer(int alUpdatesPerSec, iLowLevelSystem *apLowLevelSystem, double (*apClock)())
	{
		mlMaxUpdates = alUpdatesPerSec/10 > 0 ? alUpdatesPerSec/10 : 1;
		mlUpdateCount =0;
		
		mpLowLevelSystem = apLowLevelSystem;

		mfSpeedMul = 1.0f;
		mpClock = apClock ? apClock : &cLogicTimer::GetMonotonicTime;

		SetUpdatesPerSec(alUpdatesPerSec);
	}

	//-----------------------------------------------------------------------

	cLogicTimer::~cLogicTimer()
	{
	}

	//-----------------------------------------------------------------------

	//////////////////////////////////////////////////////////////////////////
	// PUBLIC METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------
	void cLogicTimer::Reset()
	{
		mfLastTime = mpClock();
		mlLocalTime = mfLastTime;
		mfAccumulator = 0.0;
		mlUpdateCount = 0;
		mbInUpdateLoop = false;
	}

	//-----------------------------------------------------------------------

	bool cLogicTimer::WantUpdate()
	{
		// Sample once per outer loop: simulation cost cannot create an unbounded
		// stream of catch-up steps, and every rendered frame has one stable alpha.
		if(!mbInUpdateLoop)
		{
			AccumulateTime();
			mbInUpdateLoop = true;
		}
		if(mlUpdateCount >= mlMaxUpdates || mfSpeedMul == 0.0 ||
			mfAccumulator + 1e-9 < mlLocalTimeAdd) return false;

		mfAccumulator -= mlLocalTimeAdd;
		if(mfAccumulator < 0.0) mfAccumulator = 0.0;
		mlLocalTime += mlLocalTimeAdd / mfSpeedMul;
		++mlUpdateCount;
		return true;
	}
	
	//-----------------------------------------------------------------------

	void cLogicTimer::EndUpdateLoop()
	{
		// Drop whole overdue steps after a stall, retaining the substep remainder.
		// The next frame must not restart interpolation from zero on every overload.
		if(mlUpdateCount >= mlMaxUpdates && mfAccumulator >= mlLocalTimeAdd)
			mfAccumulator = std::fmod(mfAccumulator, mlLocalTimeAdd);

		mlUpdateCount=0;
		mbInUpdateLoop = false;
	}

	//-----------------------------------------------------------------------

	void cLogicTimer::SetUpdatesPerSec(int alUpdatesPerSec)
	{
		if(alUpdatesPerSec < 1) alUpdatesPerSec = 1;
		mlLocalTimeAdd = 1000.0/((double)alUpdatesPerSec);
		Reset();
	}

	//-----------------------------------------------------------------------

	void cLogicTimer::SetMaxUpdates(int alMax)
	{
		mlMaxUpdates = alMax > 0 ? alMax : 1;
	}

	//-----------------------------------------------------------------------

	int cLogicTimer::GetUpdatesPerSec()
	{
		return (int)(1000.0 / mlLocalTimeAdd + 0.5);
	}
	
	//-----------------------------------------------------------------------

	float cLogicTimer::GetStepSize()
	{
		return ((float)mlLocalTimeAdd)/1000.0f;
	}

	//-----------------------------------------------------------------------
	
	//////////////////////////////////////////////////////////////////////////
	// PRIVATE METHODS
	//////////////////////////////////////////////////////////////////////////

	//-----------------------------------------------------------------------

	float cLogicTimer::GetInterpolationAmount() const
	{
		double fAlpha = mfAccumulator / mlLocalTimeAdd;
		return (float)(fAlpha < 0.0 ? 0.0 : (fAlpha > 1.0 ? 1.0 : fAlpha));
	}

	void cLogicTimer::SetSpeedMul(float afX)
	{
		// Time already elapsed belongs to the old speed. A zero multiplier freezes
		// simulation without a division by zero; the fixed step itself never changes.
		AccumulateTime();
		mfSpeedMul = std::isfinite(afX) && afX > 0.0f ? afX : 0.0;
	}

	void cLogicTimer::AccumulateTime()
	{
		double fNow = mpClock();
		double fElapsed = fNow - mfLastTime;
		if(fElapsed > 0.0) mfAccumulator += fElapsed * mfSpeedMul;
		mfLastTime = fNow;
	}

	double cLogicTimer::GetMonotonicTime()
	{
		return std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	//-----------------------------------------------------------------------

}
