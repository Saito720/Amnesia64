#include "graphics/PostEffect_ImageTrail.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace hpl;

namespace
{
	void Check(bool condition, const char* message)
	{
		if(condition) return;
		std::fprintf(stderr,"FAIL: %s\n",message);
		std::exit(1);
	}

	double Retention(float amount, float step)
	{
		return 1.0 - cPostEffect_ImageTrail::GetFrameBlendAlpha(amount, step);
	}
}

int main()
{
	const float amounts[] = {0.01f, 0.3f, 0.9f, 1.6f, 3.0f, 8.0f};
	const int rates[] = {30, 60, 120, 144, 240, 360, 1000};
	for(float amount : amounts)
	{
		// Freeze the retail equation at its original rendering rate as the
		// behavioral oracle, independently of the new elapsed-time conversion.
		const double retailAlpha = std::exp(-(1.0 / (1.0 / 60.0)) * amount * 0.015);
		Check(std::fabs(cPostEffect_ImageTrail::GetFrameBlendAlpha(amount,1.0f/60.0f)-retailAlpha)<0.0000002,
			"The original 60 Hz image trail strength must be preserved");
		const double expected = std::pow(1.0-retailAlpha,6.0);
		for(int rate : rates)
		{
			double history = 1.0;
			const double step = 1.0/rate;
			double remaining = 0.1;
			while(remaining>0.000000001)
			{
				const double elapsed = remaining<step ? remaining : step;
				history *= Retention(amount,static_cast<float>(elapsed));
				remaining -= elapsed;
			}
			Check(std::fabs(history-expected)<0.000005,
				"History after equal elapsed time must agree with the retail 60 Hz decay at every FPS");
		}
		const float jitter[] = {0.001f, 0.021f, 0.003f, 0.04f, 0.015f, 0.02f};
		double history = 1.0;
		for(float step : jitter) history *= Retention(amount,step);
		Check(std::fabs(history-expected)<0.000005,
			"Varying frame times must preserve the same elapsed-time response");
	}

	Check(cPostEffect_ImageTrail::GetFrameBlendAlpha(0,1.0f/1000)==1,
		"Zero image trail amount must show the current frame without history");
	Check(cPostEffect_ImageTrail::GetFrameBlendAlpha(-1,1.0f/60)==1,
		"Negative image trail amount must remain a safe no-trail value");
	Check(cPostEffect_ImageTrail::GetFrameBlendAlpha(1.6f,0)==0 &&
		cPostEffect_ImageTrail::GetFrameBlendAlpha(1.6f,-1)==0,
		"Zero or negative elapsed time must hold image history");
	Check(cPostEffect_ImageTrail::GetFrameBlendAlpha(1.6f,10)>0.99999f,
		"A long stalled frame must settle old history instead of keeping a stale image");
	Check(cPostEffect_ImageTrail::GetFrameBlendAlpha(1.6f,0.000001f)>0,
		"Very short frame durations must retain enough numeric precision to advance history");
	Check(cPostEffect_ImageTrail::GetFrameBlendAlpha(std::numeric_limits<float>::quiet_NaN(),1.0f/60)==1 &&
		cPostEffect_ImageTrail::GetFrameBlendAlpha(1.6f,std::numeric_limits<float>::infinity())==1 &&
		cPostEffect_ImageTrail::GetFrameBlendAlpha(1.6f,std::numeric_limits<float>::quiet_NaN())==0,
		"Malformed amount/timing values must not send NaN into the feedback buffer");
	std::puts("PASS: image trail preserves retail 60 Hz response across 30-1000 FPS, jitter, stalls and edge values");
	return 0;
}
