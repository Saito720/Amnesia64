#include "scene/Camera.h"
#include "scene/Entity3D.h"
#include "math/Math.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace hpl;

namespace
{
	class cAttachedEntity : public iEntity3D
	{
	public:
		cAttachedEntity() : iEntity3D("camera attachment") {}
		tString GetEntityType() { return "test"; }
	};

	void Check(bool condition, const char* message)
	{
		if(!condition)
		{
			std::fprintf(stderr, "FAIL: %s\n", message);
			std::exit(1);
		}
	}

	bool Near(float a, float b, float epsilon = 0.0001f)
	{
		return std::fabs(a - b) < epsilon;
	}

	bool NearVector(const cVector3f& a, const cVector3f& b)
	{
		return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
	}

	bool NearMatrix(const cMatrixf& a, const cMatrixf& b)
	{
		for(int row = 0; row < 4; ++row)
			for(int col = 0; col < 4; ++col)
				if(!Near(a.m[row][col], b.m[row][col])) return false;
		return true;
	}
}

int main()
{
	cCamera camera;
	camera.SetPosition(cVector3f(10, 20, 30));
	camera.SetYaw(cMath::ToRad(179));
	camera.SetFOV(cMath::ToRad(60));
	camera.SetAspect(1.0f);
	camera.SetNearClipPlane(0.1f);
	camera.SetFarClipPlane(100.0f);
	Check(NearVector(camera.GetRenderFrustum(0)->GetOrigin(), camera.GetPosition()),
		"A newly created camera must render its current pose");
	const cMatrixf previousView = camera.GetViewMatrix();
	camera.BeginInterpolationStep();
	camera.SetPosition(cVector3f(14, 28, 42));
	camera.SetYaw(cMath::ToRad(-179));
	camera.SetFOV(cMath::ToRad(80));
	camera.SetAspect(2.0f);
	camera.SetNearClipPlane(0.3f);
	camera.SetFarClipPlane(200.0f);

	const cMatrixf currentView = camera.GetViewMatrix();
	const cMatrixf currentProjection = camera.GetProjectionMatrix();
	const cMatrixf attachmentMatrix = camera.GetAttachmentNode()->GetWorldMatrix();
	cFrustum* authoritativeFrustum = camera.GetFrustum();
	Check(NearMatrix(camera.GetRenderFrustum(0)->GetViewMatrix(), previousView),
		"Alpha zero must preserve the previous camera view");
	Check(NearMatrix(camera.GetRenderFrustum(1)->GetViewMatrix(), currentView),
		"Alpha one must preserve the current camera view");
	Check(NearMatrix(camera.GetRenderFrustum(-0.5f)->GetViewMatrix(), previousView),
		"Negative interpolation alpha must clamp to the previous camera view");
	Check(NearMatrix(camera.GetRenderFrustum(2.0f)->GetViewMatrix(), currentView),
		"Interpolation alpha above one must clamp to the current camera view");

	cFrustum* rendered = camera.GetRenderFrustum(0.5f);
	Check(NearVector(rendered->GetOrigin(), cVector3f(12, 24, 36)),
		"Camera position must interpolate between fixed updates");
	// Frustum::GetForward returns the view matrix's +Z axis (the reverse of
	// Camera::GetForward), so the short arc crosses a negative Z basis here.
	Check(rendered->GetForward().z < -0.999f,
		"Yaw wrapping from 179 to -179 degrees must take the short arc");
	Check(Near(rendered->GetFOV(), cMath::ToRad(70)) && Near(rendered->GetAspect(), 1.5f),
		"FOV and aspect animation must interpolate");
	Check(NearMatrix(rendered->GetProjectionMatrix(),
		cMath::MatrixPerspectiveProjection(0.2f, 150.0f, cMath::ToRad(70), 1.5f, false)),
		"The projection matrix must use interpolated parameters");
	Check(NearVector(camera.GetPosition(), cVector3f(14, 28, 42)) &&
		Near(camera.GetYaw(), cMath::ToRad(-179)) && Near(camera.GetFOV(), cMath::ToRad(80)),
		"Rendering must not modify authoritative camera pose or lens values");
	Check(NearMatrix(camera.GetViewMatrix(), currentView) &&
		NearMatrix(camera.GetProjectionMatrix(), currentProjection),
		"Rendering must not overwrite authoritative matrix caches");
	Check(camera.GetFrustum() == authoritativeFrustum &&
		NearVector(authoritativeFrustum->GetOrigin(), camera.GetPosition()),
		"The gameplay frustum must remain at the authoritative pose");
	Check(NearMatrix(camera.GetAttachmentNode()->GetWorldMatrix(), attachmentMatrix),
		"Rendering must not move the camera attachment node");

	const cVector3f quarter = camera.GetRenderFrustum(0.25f)->GetOrigin();
	const cVector3f threeQuarters = camera.GetRenderFrustum(0.75f)->GetOrigin();
	Check(!NearVector(quarter, threeQuarters),
		"Two renders of one simulation interval must produce distinct moving views");
	Check(NearVector(camera.GetRenderFrustum(0.25f)->GetOrigin(), quarter),
		"Repeated rendering must not accumulate interpolation into the simulation");

	cAttachedEntity attachment;
	attachment.SetPosition(cVector3f(1, 0, 0));
	camera.AttachEntity(&attachment);
	iEntity3D::BeginRenderInterpolation(1.0f);
	attachment.GetRenderWorldMatrix();
	iEntity3D::EndRenderInterpolation();
	iEntity3D::CaptureInterpolationState();
	camera.ResetInterpolation();
	camera.SetPosition(cVector3f(1000, 2000, 3000));
	Check(NearVector(camera.GetRenderFrustum(0.25f)->GetOrigin(), camera.GetPosition()),
		"A teleport must discard the previous pose, including setters after reset");
	iEntity3D::BeginRenderInterpolation(0.25f);
	Check(NearVector(attachment.GetRenderWorldPosition(), attachment.GetWorldPosition()),
		"Camera-attached objects must discard old poses when the camera teleports");
	iEntity3D::EndRenderInterpolation();
	camera.BeginInterpolationStep();
	camera.SetPosition(cVector3f(1004, 2000, 3000));
	Check(NearVector(camera.GetRenderFrustum(0.5f)->GetOrigin(), cVector3f(1002, 2000, 3000)),
		"Interpolation must resume on the next ordinary simulation step");
	camera.BeginInterpolationStep();
	camera.SetPosition(cVector3f(1008, 2000, 3000));
	Check(NearVector(camera.GetRenderFrustum(0.5f)->GetOrigin(), cVector3f(1006, 2000, 3000)),
		"Multiple fixed updates must retain only the most recent pair of poses");

	camera.SetRotateMode(eCameraRotateMode_Matrix);
	camera.SetRotationMatrix(cMath::MatrixRotateY(cMath::ToRad(170)));
	camera.BeginInterpolationStep();
	camera.SetRotationMatrix(cMath::MatrixRotateY(cMath::ToRad(-170)));
	Check(camera.GetRenderFrustum(0.5f)->GetForward().z < -0.999f,
		"Matrix-mode rotations must also interpolate around the short arc");
	camera.SetProjectionType(eProjectionType_Orthographic);
	camera.SetOrthoViewSize(cVector2f(4, 2));
	camera.BeginInterpolationStep();
	camera.SetOrthoViewSize(cVector2f(8, 6));
	rendered = camera.GetRenderFrustum(0.5f);
	Check(Near(rendered->GetOrthoViewSize().x, 6) && Near(rendered->GetOrthoViewSize().y, 4),
		"Orthographic camera size must interpolate");

	std::puts("Camera interpolation checks passed.");
	return 0;
}
