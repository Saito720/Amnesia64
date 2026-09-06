#include "scene/Entity3D.h"
#include "scene/Node3D.h"
#include "math/TransformInterpolation.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace hpl;

static void Require(bool condition, const char* message)
{
	if(!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}

static bool Near(float a, float b, float epsilon=0.0001f) { return std::fabs(a-b) < epsilon; }
static bool Near(const cVector3f& a, const cVector3f& b)
{
	return Near(a.x,b.x) && Near(a.y,b.y) && Near(a.z,b.z);
}

class cTestEntity : public iEntity3D
{
public:
	cTestEntity() : iEntity3D("interpolation test") { mBoundingVolume.SetSize(cVector3f(2)); }
	tString GetEntityType() { return "Test"; }
};

class cCountCallback : public iEntityCallback
{
public:
	cCountCallback() : count(0) {}
	void OnTransformUpdate(iEntity3D*) { ++count; }
	int count;
};

static void TestTransformMath()
{
	const float pi = 3.14159265358979323846f;
	cMatrixf first = cMath::MatrixMul(cMath::MatrixRotateY(170*pi/180),cMath::MatrixScale(cVector3f(2,3,4)));
	cMatrixf second = cMath::MatrixMul(cMath::MatrixRotateY(-170*pi/180),cMath::MatrixScale(cVector3f(4,5,6)));
	first.SetTranslation(cVector3f(2,4,6));
	second.SetTranslation(cVector3f(4,8,12));
	Require(InterpolateTransform(first,second,0) == first,"alpha zero preserves exact matrix");
	Require(InterpolateTransform(first,second,1) == second,"alpha one preserves exact matrix");
	cMatrixf middle = InterpolateTransform(first,second,0.5f);
	Require(Near(middle.GetTranslation(),cVector3f(3,6,9)),"translation interpolation");
	Require(Near(middle.m[0][0],-3) && Near(middle.m[1][1],4) && Near(middle.m[2][2],-5),
		"shortest quaternion arc and nonuniform scale");

	first = cMath::MatrixScale(cVector3f(-2,3,4));
	second = cMath::MatrixMul(cMath::MatrixRotateY(pi/2),cMath::MatrixScale(cVector3f(-4,5,6)));
	middle = InterpolateTransform(first,second,0.5f);
	Require(cMath::Vector3Dot(cMath::Vector3Cross(middle.GetRight(),middle.GetUp()),middle.GetForward()) < 0,
		"reflection survives interpolation");
	first = cMath::MatrixScale(cVector3f(0,1,1));
	middle = InterpolateTransform(first,cMatrixf::Identity,0.5f);
	Require(Near(middle.m[0][0],0.5f),"degenerate scale remains finite");
	first = cMatrixf::Identity; first.m[0][1] = 0.4f;
	middle = InterpolateTransform(first,cMatrixf::Identity,0.5f);
	Require(Near(middle.m[0][1],0.2f),"sheared transform preserves affine data");
}

static void TestAuthoritativeStateAndLifecycle()
{
	cTestEntity entity;
	cCountCallback callback;
	entity.AddCallback(&callback);
	iEntity3D::BeginRenderInterpolation(1);
	entity.GetRenderWorldMatrix(); // First observation establishes history.
	iEntity3D::EndRenderInterpolation();
	iEntity3D::CaptureInterpolationState();
	entity.SetPosition(cVector3f(10,0,0));
	const int updates = callback.count;
	const int simulationCount = entity.GetTransformUpdateCount();
	int lastRenderCount = 0;
	for(int frame=0; frame<=4; ++frame)
	{
		iEntity3D::BeginRenderInterpolation(frame/4.0f);
		Require(Near(entity.GetRenderWorldPosition(),cVector3f(frame*2.5f,0,0)),"distinct intermediate rendered positions");
		Require(Near(entity.GetWorldPosition(),cVector3f(10,0,0)),"rendering never alters simulation pose");
		Require(callback.count == updates && entity.GetTransformUpdateCount() == simulationCount,"no physics callbacks or authoritative invalidation on render");
		int renderCount = entity.GetRenderTransformUpdateCount();
		Require(renderCount != lastRenderCount,"render matrix cache invalidates between interpolated frames");
		Require(entity.GetRenderTransformUpdateCount() == renderCount,"matrix cache stable within one frame");
		lastRenderCount = renderCount;
		cBoundingVolume* bounds = entity.GetRenderBoundingVolume();
		Require(bounds->GetMin().x <= -1 && bounds->GetMax().x >= 11,"bounds cover both fixed endpoints");
		iEntity3D::EndRenderInterpolation();
	}
	Require(Near(entity.GetRenderWorldPosition(),entity.GetWorldPosition()),"outside rendering getters return simulation state");

	iEntity3D::CaptureInterpolationState(); entity.SetPosition(cVector3f(20,0,0));
	iEntity3D::CaptureInterpolationState(); entity.SetPosition(cVector3f(30,0,0));
	iEntity3D::BeginRenderInterpolation(0.25f);
	Require(Near(entity.GetRenderWorldPosition(),cVector3f(22.5f,0,0)),"catch-up ticks retain the latest interval");
	iEntity3D::EndRenderInterpolation();
	entity.SetPosition(cVector3f(100,0,0)); entity.ResetRenderInterpolation();
	iEntity3D::BeginRenderInterpolation(0);
	Require(Near(entity.GetRenderWorldPosition(),cVector3f(100,0,0)),"teleport reset snaps presentation");
	iEntity3D::EndRenderInterpolation();

	iEntity3D::CaptureInterpolationState(); entity.SetPosition(cVector3f(200,0,0));
	iEntity3D::ResetInterpolationState();
	iEntity3D::BeginRenderInterpolation(0.1f);
	Require(Near(entity.GetRenderWorldPosition(),cVector3f(200,0,0)),"global load/reset clears stale history");
	{
		cTestEntity spawned;
		spawned.SetPosition(cVector3f(37,2,1));
		Require(Near(spawned.GetRenderWorldPosition(),cVector3f(37,2,1)),"new object never interpolates from origin");
	}
	iEntity3D::EndRenderInterpolation();
	iEntity3D::CaptureInterpolationState(); // Destroyed object has unregistered.
	entity.RemoveCallback(&callback);
}

static void TestHierarchy()
{
	cTestEntity parent, attached;
	cNode3D bone("bone",false);
	parent.AddNodeChild(&bone);
	bone.SetPosition(cVector3f(2,0,0));
	bone.AddEntity(&attached);
	iEntity3D::BeginRenderInterpolation(1);
	attached.GetRenderWorldMatrix();
	iEntity3D::EndRenderInterpolation();
	iEntity3D::CaptureInterpolationState();
	parent.SetMatrix(cMath::MatrixRotateZ(3.14159265358979323846f/2));
	iEntity3D::BeginRenderInterpolation(0.5f);
	cVector3f position = attached.GetRenderWorldPosition();
	Require(Near(position,cVector3f(std::sqrt(2.0f),std::sqrt(2.0f),0)),"node hierarchy rotates at interpolated parent pose");
	Require(Near(position.Length(),2),"bone length remains constant through rotation");
	Require(Near(attached.GetWorldPosition(),cVector3f(0,2,0)),"attached simulation pose remains authoritative");
	iEntity3D::EndRenderInterpolation();
	parent.SetPosition(cVector3f(100,0,0));
	parent.ResetRenderInterpolation();
	iEntity3D::BeginRenderInterpolation(0);
	Require(Near(attached.GetRenderWorldPosition(),attached.GetWorldPosition()),"hierarchy reset propagates through bone and entity descendants");
	iEntity3D::EndRenderInterpolation();
}

int main()
{
	TestTransformMath();
	TestAuthoritativeStateAndLifecycle();
	TestHierarchy();
	iEntity3D::ResetInterpolationState();
	std::puts("Transform interpolation: math, hierarchy, bounds, lifecycle and authoritative-state checks passed.");
	return 0;
}
