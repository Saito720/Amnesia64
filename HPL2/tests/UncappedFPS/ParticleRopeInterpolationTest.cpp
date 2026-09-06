#include "scene/ParticleEmitter.h"
#include "physics/PhysicsRope.h"
#include "physics/VerletParticle.h"
#include "impl/PhysicsWorldNewton.h"
#include "math/Math.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace hpl;

namespace
{
	void Check(bool condition, const char* message)
	{
		if(condition) return;
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}

	bool Near(float a, float b) { return std::fabs(a-b) < 0.0001f; }
	bool Near(const cVector3f& a, const cVector3f& b)
	{ return Near(a.x,b.x) && Near(a.y,b.y) && Near(a.z,b.z); }

	void SetParticle(cParticle& particle, float position)
	{
		particle.mvPos = cVector3f(position,2,3);
		particle.mvLastPos = cVector3f(position-1,2,3);
		particle.mvLastCollidePos = cVector3f(position-2,2,3);
		particle.mvSize = cVector2f(2,4);
		particle.mColor = cColor(0.2f,0.4f,0.6f,0.8f);
		particle.mfSpin = k2Pif-0.1f;
	}

	void TestParticles()
	{
		cParticle particle;
		SetParticle(particle,10);
		Check(Near(particle.GetRenderPosition(0),particle.mvPos), "New particles use their current position");
		particle.CaptureRenderState();
		particle.mvPos.x = 14;
		particle.mvLastPos.x = 13;
		particle.mvSize = cVector2f(4,8);
		particle.mColor = cColor(0.6f,0.8f,1.0f,0.4f);
		particle.mfSpin = 0.1f;
		Check(Near(particle.GetRenderPosition(0).x,10) && Near(particle.GetRenderPosition(1).x,14),
			"Particle render endpoints match consecutive fixed states");
		Check(Near(particle.GetRenderPosition(0.5f).x,12) && Near(particle.GetRenderLastPosition(0.5f).x,11),
			"Particle line head and tail both interpolate");
		Check(Near(particle.GetRenderSize(0.5f).x,3) && Near(particle.GetRenderSize(0.5f).y,6),
			"Particle size interpolates");
		Check(Near(particle.GetRenderColor(0.5f).r,0.4f) && Near(particle.GetRenderColor(0.5f).a,0.6f),
			"Particle color and alpha interpolate");
		Check(Near(particle.GetRenderSpin(0.5f),k2Pif), "Spin interpolation takes the short path across wrapping");
		for(int frame=0; frame<100; ++frame)
		{
			const float alpha = static_cast<float>(frame%10)/10;
			particle.GetRenderPosition(alpha);
			particle.GetRenderLastPosition(alpha);
			particle.GetRenderSize(alpha);
			particle.GetRenderColor(alpha);
			particle.GetRenderSpin(alpha);
		}
		Check(Near(particle.mvPos.x,14) && Near(particle.mvLastPos.x,13) && Near(particle.mvLastCollidePos.x,8),
			"Additional frames cannot modify particle simulation or collision history");
		Check(Near(particle.mfSpin,0.1f) && Near(particle.mvSize.x,4) && Near(particle.mColor.r,0.6f),
			"Additional frames cannot modify particle spin, size or color");
		particle.CaptureRenderState();
		Check(Near(particle.GetRenderPosition(0).x,14) && Near(particle.GetRenderPosition(0.7f).x,14),
			"A sleeping particle settles instead of replaying its final movement");
		SetParticle(particle,-100);
		particle.CaptureRenderState();
		Check(Near(particle.GetRenderPosition(0).x,-100) && Near(particle.GetRenderPosition(0.5f).x,-100),
			"Respawning particles do not interpolate from a previous life");
	}

	void TestRopes()
	{
		cPhysicsWorldNewton baselineWorld;
		cPhysicsWorldNewton renderedWorld;
		iPhysicsRope* baseline = baselineWorld.CreateRope("baseline",cVector3f(0,1,0),cVector3f(1,1,0));
		iPhysicsRope* rendered = renderedWorld.CreateRope("rendered",cVector3f(0,1,0),cVector3f(1,1,0));
		baseline->SetSegmentLength(0.2f);
		rendered->SetSegmentLength(0.2f);
		bool sawIntermediatePosition = false;
		for(int tick=0; tick<120; ++tick)
		{
			baselineWorld.Update(1.0f/60.0f);
			renderedWorld.Update(1.0f/60.0f);
			cVerletParticleIterator baseIt = baseline->GetParticleIterator();
			cVerletParticleIterator renderIt = rendered->GetParticleIterator();
			while(baseIt.HasNext() && renderIt.HasNext())
			{
				cVerletParticle* baseParticle = baseIt.Next();
				cVerletParticle* particle = renderIt.Next();
				const cVector3f position = particle->GetPosition();
				const cVector3f previousPhysicsPosition = particle->GetPrevPosition();
				const cVector3f smoothPosition = particle->GetSmoothPosition();
				const cVector3f previousRenderPosition = particle->GetRenderPosition(0);
				Check(Near(particle->GetRenderPosition(1),smoothPosition), "Rope current render endpoint is its smooth position");
				Check(Near(particle->GetRenderPosition(0.5f),(previousRenderPosition+smoothPosition)*0.5f),
					"Rope positions interpolate between fixed snapshots");
				if(!Near(previousRenderPosition,smoothPosition)) sawIntermediatePosition = true;
				for(int frame=0; frame<8; ++frame) particle->GetRenderPosition(static_cast<float>(frame)/8);
				Check(Near(particle->GetPosition(),position) && Near(particle->GetPrevPosition(),previousPhysicsPosition) &&
					Near(particle->GetSmoothPosition(),smoothPosition), "Rendering leaves Verlet integration state unchanged");
				Check(Near(particle->GetPosition(),baseParticle->GetPosition()) &&
					Near(particle->GetPrevPosition(),baseParticle->GetPrevPosition()),
					"Rope trajectories are identical with zero or eight render samples per fixed tick");
			}
			Check(!baseIt.HasNext() && !renderIt.HasNext(), "Both rope simulations have identical topology");
		}
		Check(sawIntermediatePosition, "Rope samples include movement between simulation states");
		cVerletParticle newborn(NULL,cVector3f(7,8,9),1);
		Check(Near(newborn.GetRenderPosition(0),cVector3f(7,8,9)), "New rope particles have initialized presentation history");
		newborn.SetSmoothPosition(cVector3f(10,11,12));
		newborn.CaptureRenderState();
		Check(Near(newborn.GetRenderPosition(0.4f),cVector3f(10,11,12)), "Restored rope particles settle at their saved position");
	}
}

int main()
{
	TestParticles();
	TestRopes();
	std::puts("PASS: particle interpolation, birth/respawn, spin wrapping, and unchanged Verlet trajectories");
	return 0;
}
