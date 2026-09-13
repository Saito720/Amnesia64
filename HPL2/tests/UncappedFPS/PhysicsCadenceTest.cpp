#include "impl/PhysicsBodyNewton.h"
#include "impl/PhysicsWorldNewton.h"
#include "physics/CollideShape.h"
#include "physics/CharacterBody.h"
#include "scene/Camera.h"
#include "system/LogicTimer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace hpl;

namespace
{
	double gClockMilliseconds = 0;
	double ReadClock() { return gClockMilliseconds; }

	void Check(bool condition, const char* message)
	{
		if(!condition)
		{
			std::fprintf(stderr, "FAIL: %s\n", message);
			std::exit(1);
		}
	}

	bool NearVector(const cVector3f& a, const cVector3f& b)
	{
		return (a - b).Length() < 0.0001f;
	}

	class cCharacterMesh : public iEntity3D
	{
	public:
		cCharacterMesh() : iEntity3D("character mesh") {}
		tString GetEntityType() { return "test"; }
	};

	void CheckCharacterTransitions()
	{
		cCamera camera;
		cCharacterMesh mesh;
		cPhysicsWorldNewton world;
		world.SetWorld(NULL);
		world.SetSaveContactPoints(false);
		world.SetNumberOfThreads(1);
		world.SetWorldSize(cVector3f(-1000), cVector3f(1000));
		iCharacterBody* character = world.CreateCharacterBody("character", cVector3f(1, 2, 1));
		character->SetGravityActive(false);
		character->SetTestCollision(false);
		character->SetCameraSmoothPosNum(0);
		character->SetEntitySmoothPosNum(0);
		character->SetCamera(&camera);
		character->SetEntity(&mesh);
		character->SetPosition(cVector3f(0, 2, 0));
		character->Update(1.0f / 60.0f);
		iEntity3D::BeginRenderInterpolation(1.0f);
		mesh.GetRenderWorldMatrix();
		iEntity3D::EndRenderInterpolation();

		for(int step = 1; step <= 2; ++step)
		{
			iEntity3D::CaptureInterpolationState();
			camera.BeginInterpolationStep();
			// Ladder movement clears the old camera smoothing list each update,
			// but still needs continuous presentation between its scripted poses.
			character->SetPosition(cVector3f(step * 4.0f, 2, 0), false, false);
			character->Update(1.0f / 60.0f);
			Check(std::fabs(camera.GetRenderFrustum(0.5f)->GetOrigin().x - (step * 4 - 2)) < 0.0001f,
				"Continuous ladder positioning must preserve camera interpolation");
			iEntity3D::BeginRenderInterpolation(0.5f);
			Check(std::fabs(mesh.GetRenderWorldPosition().x - (step * 4 - 2)) < 0.0001f,
				"Continuous character positioning must preserve attached entity interpolation");
			iEntity3D::EndRenderInterpolation();
		}

		// Teleport before the next tick captures its history. Attachment transforms
		// still have the old position until CharacterBody::Update runs below.
		character->SetPosition(cVector3f(100, 2, 0));
		iEntity3D::CaptureInterpolationState();
		camera.BeginInterpolationStep();
		character->Update(1.0f / 60.0f);
		Check(NearVector(camera.GetRenderFrustum(0.5f)->GetOrigin(), camera.GetPosition()),
			"A pre-update character teleport must discard the stale synchronized camera pose");
		iEntity3D::BeginRenderInterpolation(0.5f);
		Check(NearVector(mesh.GetRenderWorldPosition(), mesh.GetWorldPosition()),
			"A pre-update character teleport must discard the stale attached mesh pose");
		iEntity3D::EndRenderInterpolation();

		iEntity3D::CaptureInterpolationState();
		camera.BeginInterpolationStep();
		character->SetFeetPosition(cVector3f(104, 1, 0), false, false);
		character->Update(1.0f / 60.0f);
		Check(std::fabs(camera.GetRenderFrustum(0.5f)->GetOrigin().x - 102.0f) < 0.0001f,
			"Continuous feet positioning must also retain interpolation");
		std::puts("Character teleport and continuous movement checks passed.");
	}

	struct cResult
	{
		cMatrixf matrix;
		cVector3f velocity;
		cVector3f angularVelocity;
		int steps;
		int changingFramesWithoutUpdate;
	};

	cResult RunSchedule(int renderFPS)
	{
		gClockMilliseconds = 0;
		cLogicTimer timer(60, NULL, &ReadClock);
		cPhysicsWorldNewton world;
		world.SetWorld(NULL);
		world.SetSaveContactPoints(false);
		world.SetNumberOfThreads(1);
		world.SetWorldSize(cVector3f(-1000), cVector3f(1000));
		world.SetGravity(cVector3f(0, -9.81f, 0));
		world.SetMaxTimeStep(1.0f / 60.0f);

		iPhysicsBody* floor = world.CreateBody("floor", world.CreateBoxShape(cVector3f(100, 1, 100), NULL));
		floor->SetMass(0);
		floor->SetPosition(cVector3f(0, -1, 0));
		floor->SetUseSurfaceEffects(false);
		cPhysicsBodyNewton* body = static_cast<cPhysicsBodyNewton*>(world.CreateBody(
			"moving", world.CreateBoxShape(cVector3f(1, 1, 1), NULL)));
		body->SetMass(2);
		body->SetPosition(cVector3f(0, 10, 0));
		body->SetLinearVelocity(cVector3f(1, 0, 0));
		body->SetLinearDamping(0.001f);
		body->SetAngularDamping(0.001f);
		body->SetAutoDisable(false);
		body->SetGravity(true);
		body->SetUseSurfaceEffects(false);

		// Enroll the body as a renderable before the first fixed update, just as
		// the engine does when its mesh is first submitted to a viewport.
		iEntity3D::BeginRenderInterpolation(1.0f);
		cVector3f previousRenderedPosition = body->GetRenderWorldPosition();
		iEntity3D::EndRenderInterpolation();
		iEntity3D::ResetInterpolationState();

		cResult result;
		result.steps = 0;
		result.changingFramesWithoutUpdate = 0;
		const int durationSeconds = 4;
		for(int frame = 0; frame <= durationSeconds * renderFPS; ++frame)
		{
			gClockMilliseconds = frame * 1000.0 / renderFPS;
			const int stepsBeforeFrame = result.steps;
			while(timer.WantUpdate())
			{
				Check(std::fabs(timer.GetStepSize() - 1.0f / 60.0f) < 0.00000001f,
					"Every physics update must retain the 60 Hz simulation timestep");
				iEntity3D::CaptureInterpolationState();
				// Exercise force accumulation, gravity, contact response, torque, and
				// one-shot impulses. None of these inputs is issued from rendering.
				body->AddForce(cVector3f(result.steps < 120 ? 6.0f : -2.0f, 3.0f, 0));
				body->AddTorque(cVector3f(0, 0.2f, 0));
				if(result.steps == 40) body->AddImpulse(cVector3f(1, 2, 0));
				if(result.steps == 180) body->AddImpulse(cVector3f(-1, 9, 0));
				world.Update(timer.GetStepSize());
				++result.steps;
			}
			timer.EndUpdateLoop();

			const cMatrixf authoritativeMatrix = body->GetWorldMatrix();
			const cVector3f authoritativeVelocity = body->GetLinearVelocity();
			const cVector3f authoritativeAngularVelocity = body->GetAngularVelocity();
			dFloat newtonBefore[16], newtonAfter[16];
			NewtonBodyGetMatrix(body->GetNewtonBody(), newtonBefore);
			iEntity3D::BeginRenderInterpolation(timer.GetInterpolationAmount());
			const cVector3f renderedPosition = body->GetRenderWorldPosition();
			body->GetRenderBoundingVolume();
			body->GetRenderTransformUpdateCount();
			Check(body->GetWorldMatrix() == authoritativeMatrix,
				"Interpolated rendering must leave the authoritative body matrix intact");
			Check(body->GetLinearVelocity() == authoritativeVelocity &&
				body->GetAngularVelocity() == authoritativeAngularVelocity,
				"Interpolated rendering must leave physics velocities intact");
			NewtonBodyGetMatrix(body->GetNewtonBody(), newtonAfter);
			Check(std::memcmp(newtonBefore, newtonAfter, sizeof(newtonBefore)) == 0,
				"Reading a render pose must not send an interpolated transform into Newton");
			iEntity3D::EndRenderInterpolation();
			if(stepsBeforeFrame == result.steps && !NearVector(renderedPosition, previousRenderedPosition))
				++result.changingFramesWithoutUpdate;
			previousRenderedPosition = renderedPosition;
		}
		Check(result.steps == durationSeconds * 60,
			"Four seconds must produce exactly 240 physics updates at every rendering rate");
		if(renderFPS > 60)
			Check(result.changingFramesWithoutUpdate > 0,
				"High FPS rendering must produce moving frames between physics updates");
		result.matrix = body->GetWorldMatrix();
		result.velocity = body->GetLinearVelocity();
		result.angularVelocity = body->GetAngularVelocity();
		Check(result.matrix.GetTranslation().y < 10.0f,
			"The test body must actually respond to gravity");
		std::printf("%4d FPS: %d fixed updates, %d changing frames without an update\n",
			renderFPS, result.steps, result.changingFramesWithoutUpdate);
		return result;
	}
}

int main()
{
	CheckCharacterTransitions();
	const cResult baseline = RunSchedule(60);
	const int rates[] = {30, 120, 144, 240, 1000};
	for(unsigned int i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i)
	{
		const cResult result = RunSchedule(rates[i]);
		Check(result.steps == baseline.steps, "Rendering cadence must not alter fixed update count");
		Check(NearVector(result.matrix.GetTranslation(), baseline.matrix.GetTranslation()),
			"Rendering cadence must not alter body position");
		for(int row = 0; row < 3; ++row)
			for(int col = 0; col < 3; ++col)
				Check(std::fabs(result.matrix.m[row][col] - baseline.matrix.m[row][col]) < 0.0001f,
					"Rendering cadence must not alter body rotation");
		Check(NearVector(result.velocity, baseline.velocity),
			"Rendering cadence must not alter linear velocity");
		Check(NearVector(result.angularVelocity, baseline.angularVelocity),
			"Rendering cadence must not alter angular velocity");
	}
	std::puts("Newton physics cadence checks passed.");
	return 0;
}
