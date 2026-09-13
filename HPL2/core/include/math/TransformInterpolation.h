#ifndef HPL_TRANSFORM_INTERPOLATION_H
#define HPL_TRANSFORM_INTERPOLATION_H

#include "math/Math.h"

namespace hpl {

	// Presentation-only interpolation. Unlike MatrixSlerp, retain nonuniform scale
	// and reflected transforms, and take the shortest quaternion arc reliably.
	inline cMatrixf InterpolateTransform(const cMatrixf& aPrevious, const cMatrixf& aCurrent, float afAlpha)
	{
		if(afAlpha <= 0) return aPrevious;
		if(afAlpha >= 1 || aPrevious == aCurrent) return aCurrent;

		cMatrixf rotations[2] = { aPrevious, aCurrent };
		cVector3f scales[2];
		bool bAffineFallback = false;
		for(int n=0; n<2; ++n)
		{
			for(int col=0; col<3; ++col)
			{
				float fLength = sqrt(rotations[n].m[0][col]*rotations[n].m[0][col] +
					rotations[n].m[1][col]*rotations[n].m[1][col] + rotations[n].m[2][col]*rotations[n].m[2][col]);
				scales[n].v[col] = fLength;
				if(fLength < 0.000001f) { bAffineFallback = true; continue; }
				for(int row=0; row<3; ++row) rotations[n].m[row][col] /= fLength;
			}
			const cVector3f x(rotations[n].m[0][0],rotations[n].m[1][0],rotations[n].m[2][0]);
			const cVector3f y(rotations[n].m[0][1],rotations[n].m[1][1],rotations[n].m[2][1]);
			const cVector3f z(rotations[n].m[0][2],rotations[n].m[1][2],rotations[n].m[2][2]);
			if(cMath::Abs(cMath::Vector3Dot(x,y)) > 0.001f ||
				cMath::Abs(cMath::Vector3Dot(x,z)) > 0.001f || cMath::Abs(cMath::Vector3Dot(y,z)) > 0.001f)
				bAffineFallback = true;
			if(cMath::Vector3Dot(cMath::Vector3Cross(x,y),z) < 0)
			{
				scales[n].x = -scales[n].x;
				for(int row=0; row<3; ++row) rotations[n].m[row][0] = -rotations[n].m[row][0];
			}
		}
		// Shear and zero scale have no unique rotation decomposition. Preserve
		// their affine data instead of producing NaNs or dropping the shear.
		if(bAffineFallback)
		{
			cMatrixf result;
			for(int row=0; row<4; ++row)
				for(int col=0; col<4; ++col)
					result.m[row][col] = aPrevious.m[row][col]*(1-afAlpha) + aCurrent.m[row][col]*afAlpha;
			return result;
		}

		cQuaternion qPrevious(rotations[0]), qCurrent(rotations[1]);
		qPrevious.Normalize(); qCurrent.Normalize();
		float fDot = cMath::QuaternionDot(qPrevious,qCurrent);
		if(fDot < 0) { qCurrent = qCurrent * -1.0f; fDot = -fDot; }
		fDot = cMath::Clamp(fDot,0.0f,1.0f);
		cQuaternion rotation;
		if(fDot > 0.9995f)
			rotation = qPrevious*(1-afAlpha) + qCurrent*afAlpha;
		else
		{
			float fAngle = acos(fDot);
			rotation = qPrevious*(sin((1-afAlpha)*fAngle)/sin(fAngle)) + qCurrent*(sin(afAlpha*fAngle)/sin(fAngle));
		}
		rotation.Normalize();
		cMatrixf result = cMath::MatrixQuaternion(rotation);
		cVector3f scale = scales[0]*(1-afAlpha) + scales[1]*afAlpha;
		for(int row=0; row<3; ++row)
			for(int col=0; col<3; ++col) result.m[row][col] *= scale.v[col];
		result.SetTranslation(aPrevious.GetTranslation()*(1-afAlpha) + aCurrent.GetTranslation()*afAlpha);
		return result;
	}
}

#endif
