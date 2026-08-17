////////////////////////////////////////////////////////
// Ellipsoidal single-scattering atmosphere - fragment shader
//
// This first pass is intentionally space-view only. The proxy mesh is expected
// to have outward-facing triangles and to match avAtmosphereRadii.
////////////////////////////////////////////////////////
#version 120
#extension GL_EXT_texture_array : enable
#extension GL_ARB_shader_texture_lod : enable

const int kViewSampleCount = 12;
const int kSunSampleCount = 6;
const int kCloudSampleCount = 24;
const int kCloudViewSampleCount = 8;
const int kCloudShadowSampleCount = 8;
const float kLargeDistance = 1.0e20;
const float kPi = 3.14159265358979323846;

varying vec3 gvLocalPosition;

uniform vec3 avCameraPosition;
uniform vec3 avSunDirection;
uniform vec3 avSunColor;

uniform vec3 avGroundRadii;
uniform vec3 avAtmosphereRadii;
uniform vec3 avRayleighScattering;
uniform float afMieExtinction;
uniform float afRayleighScaleHeight;
uniform float afMieScaleHeight;
uniform float afAerosolDensity;

uniform sampler2DArray aCloudMap;
uniform float afCloudEnabled;
// xy is the virtual tile grid; zw is one array layer's pixel size.
uniform vec4 avCloudTextureLayout;
uniform float afCloudBaseHeight;
uniform float afCloudMaxHeight;
uniform float afCloudCoverageThreshold;
uniform float afCloudExtinction;

@ifdef AtmosphereScatteringPass
uniform float afMieScattering;
uniform float afMieAnisotropy;
uniform float afExposure;
uniform float afMultipleScatteringStrength;
uniform float afCloudAmbient;
uniform float afCloudSunIntensity;
uniform float afCloudTwilightStrength;
uniform vec3 avCloudTwilightColor;
uniform float afCloudSelfShadow;
@else
uniform float afCloudShadowStrength;
uniform float afCloudShadowSoftness;
@endif

vec2 RayEllipsoidIntersection(vec3 avOrigin, vec3 avDirection, vec3 avRadii)
{
	vec3 vOrigin = avOrigin / avRadii;
	vec3 vDirection = avDirection / avRadii;
	float fA = dot(vDirection, vDirection);
	float fB = 2.0 * dot(vOrigin, vDirection);
	float fC = dot(vOrigin, vOrigin) - 1.0;
	float fDiscriminant = fB * fB - 4.0 * fA * fC;

	if(fDiscriminant < 0.0)
		return vec2(kLargeDistance, -kLargeDistance);

	float fRoot = sqrt(fDiscriminant);
	float fInvTwoA = 0.5 / fA;
	return vec2((-fB - fRoot) * fInvTwoA,
				(-fB + fRoot) * fInvTwoA);
}

float HeightAboveGround(vec3 avPosition)
{
	// Radial altitude is exact on the ellipsoid surface and is a sufficiently
	// close approximation to geodetic altitude for Earth's small flattening.
	float fScaledRadius = length(avPosition / avGroundRadii);
	float fRadius = length(avPosition);
	float fGroundRadius = fRadius / max(fScaledRadius, 0.000001);
	return max(fRadius - fGroundRadius, 0.0);
}

vec2 DensityAt(vec3 avPosition)
{
	float fHeight = HeightAboveGround(avPosition);
	return vec2(exp(-fHeight / afRayleighScaleHeight),
				afAerosolDensity * exp(-fHeight / afMieScaleHeight));
}

@ifdef AtmosphereScatteringPass
bool IsPlanetShadowed(vec3 avPosition, float afAtmosphereExitDistance)
{
	vec2 vGroundHit = RayEllipsoidIntersection(
		avPosition, avSunDirection, avGroundRadii);
	return vGroundHit.x > 0.0001 && vGroundHit.x < afAtmosphereExitDistance;
}

vec2 OpticalDepthToSun(vec3 avPosition)
{
	vec2 vAtmosphereHit = RayEllipsoidIntersection(
		avPosition, avSunDirection, avAtmosphereRadii);
	float fDistance = vAtmosphereHit.y;

	if(fDistance <= 0.0 || IsPlanetShadowed(avPosition, fDistance))
		return vec2(-1.0);

	float fStep = fDistance / float(kSunSampleCount);
	vec2 vOpticalDepth = vec2(0.0);

	for(int i = 0; i < kSunSampleCount; ++i)
	{
		float fT = (float(i) + 0.5) * fStep;
		vOpticalDepth += DensityAt(avPosition + avSunDirection * fT) * fStep;
	}

	return vOpticalDepth;
}
@endif

vec3 Extinction(vec2 avOpticalDepth)
{
	return avRayleighScattering * avOpticalDepth.x +
		vec3(afMieExtinction * avOpticalDepth.y);
}

float AverageSegmentTransmittance(float afOpticalDepth)
{
	// Average exp(-tau) from the near to far edge of a segment. Use the
	// series expansion close to zero to avoid cancellation in 1 - exp(-tau).
	if(afOpticalDepth < 0.001)
		return 1.0 - 0.5 * afOpticalDepth +
			afOpticalDepth * afOpticalDepth / 6.0;

	return (1.0 - exp(-afOpticalDepth)) / afOpticalDepth;
}

vec3 AverageSegmentTransmittance(vec3 avOpticalDepth)
{
	return vec3(
		AverageSegmentTransmittance(avOpticalDepth.r),
		AverageSegmentTransmittance(avOpticalDepth.g),
		AverageSegmentTransmittance(avOpticalDepth.b));
}

float TerminatorChromaAt(vec3 avPosition)
{
	vec3 vNormal = normalize(avPosition / (avGroundRadii * avGroundRadii));
	float fSunCosine = dot(vNormal, avSunDirection);
	return 1.0 - smoothstep(
		0.139173, 0.258819, abs(fSunCosine));
}

vec3 ViewTransmissionForSolarGeometry(vec3 avTransmission, vec3 avPosition)
{
	// A long camera path may reduce brightness, but it must not manufacture a
	// sunset. Preserve spectral reddening only where the local solar elevation
	// is genuinely near the terminator (roughly within 15 degrees).
	float fTerminatorChroma = TerminatorChromaAt(avPosition);
	float fNeutralTransmission = dot(avTransmission,
		vec3(0.2126, 0.7152, 0.0722));
	return mix(vec3(fNeutralTransmission), avTransmission, fTerminatorChroma);
}

@ifdef AtmosphereScatteringPass
vec2 OpticalDepthAlongView(vec3 avOrigin, vec3 avDirection,
	float afRayStart, float afRayEnd)
{
	if(afRayEnd <= afRayStart)
		return vec2(0.0);

	float fStep = (afRayEnd - afRayStart) /
		float(kCloudViewSampleCount);
	vec2 vOpticalDepth = vec2(0.0);

	for(int i = 0; i < kCloudViewSampleCount; ++i)
	{
		float fT = afRayStart + (float(i) + 0.5) * fStep;
		vOpticalDepth += DensityAt(avOrigin + avDirection * fT) * fStep;
	}

	return vOpticalDepth;
}
@endif

vec2 CloudUv(vec3 avPosition)
{
	vec3 vDirection = normalize(avPosition);
	return vec2(
		fract(0.5 + atan(-vDirection.z, vDirection.x) / (2.0 * kPi)),
		acos(clamp(vDirection.y, -1.0, 1.0)) / kPi);
}

float GetCloudMipLevel(vec2 avScaledTexCoord)
{
	vec2 vTileSize = max(avCloudTextureLayout.zw, vec2(1.0));
	vec2 vTileGrid = max(avCloudTextureLayout.xy, vec2(1.0));
	vec2 vDxCoord = dFdx(avScaledTexCoord);
	vec2 vDyCoord = dFdy(avScaledTexCoord);

	// Longitude is periodic. Across the atan/fract seam the raw derivative jumps
	// by the entire virtual texture width, which otherwise selects a very coarse
	// mip and draws a dark meridian through the cloud layer.
	vDxCoord.x -= floor(vDxCoord.x / vTileGrid.x + 0.5) * vTileGrid.x;
	vDyCoord.x -= floor(vDyCoord.x / vTileGrid.x + 0.5) * vTileGrid.x;

	vec2 vDx = vDxCoord * vTileSize;
	vec2 vDy = vDyCoord * vTileSize;
	float fRho2 = max(dot(vDx, vDx), dot(vDy, vDy));
	return max(0.0, 0.5 * log(max(fRho2, 1.0)) * 1.4426950408889634);
}

vec3 SampleCloudArray(vec2 avUv, vec2 avTileGrid, float afMipLevel)
{
	vec2 vSafeUv = vec2(
		clamp(avUv.x, 0.0, 0.9999999),
		clamp(avUv.y, 0.0, 0.999999));
	vec2 vScaledUv = vSafeUv * avTileGrid;
	vec2 vTile = floor(vScaledUv);
	vec2 vLocalUv = vScaledUv - vTile;
	float fLayer = (avTileGrid.y - 1.0 - vTile.y) * avTileGrid.x + vTile.x;
	return texture2DArrayLod(
		aCloudMap, vec3(vLocalUv, fLayer), afMipLevel).rgb;
}

float SampleCloudLuminance(vec3 avPosition, float afMipLevel)
{
	vec2 vTileGrid = max(avCloudTextureLayout.xy, vec2(1.0));
	vec2 vTileSize = max(avCloudTextureLayout.zw, vec2(1.0));
	vec2 vUv = CloudUv(avPosition);
	vec2 vWrappedUv = vec2(vUv.x, clamp(vUv.y, 0.0, 0.999999));
	vec3 vCloud = SampleCloudArray(vWrappedUv, vTileGrid, afMipLevel);

	// Texture-array layers cannot filter across the equirectangular wrap. Emulate
	// that single periodic filtering footprint by blending the last and first
	// columns only within half a texel at the selected mip level.
	float fVirtualWidth = max(vTileSize.x * vTileGrid.x, 1.0);
	float fSeamHalfWidth = 0.5 * exp2(afMipLevel) / fVirtualWidth;
	float fSeamDistance = min(vWrappedUv.x, 1.0 - vWrappedUv.x);
	if(fSeamDistance < fSeamHalfWidth)
	{
		float fOppositeU = clamp(
			1.0 - vWrappedUv.x, 0.0, 1.0 - 0.25 / fVirtualWidth);
		vec3 vOppositeCloud = SampleCloudArray(
			vec2(fOppositeU, vWrappedUv.y), vTileGrid, afMipLevel);
		float fOppositeWeight = 0.5 *
			(1.0 - fSeamDistance / max(fSeamHalfWidth, 1.0e-8));
		vCloud = mix(vCloud, vOppositeCloud, fOppositeWeight);
	}

	return dot(vCloud, vec3(0.2126, 0.7152, 0.0722));
}

vec4 CloudPropertiesAt(vec3 avPosition, float afMipLevel,
	float afCoverageCompensation)
{
	float fLuminance = SampleCloudLuminance(avPosition, afMipLevel);
	// Coarse grazing mips average sparse cloud texels into their dark
	// surroundings. Lower the cutoff only at the strongest added blur so that
	// sub-pixel coverage survives without restoring high-frequency aliasing.
	float fCoverageThreshold = afCloudCoverageThreshold * mix(
		1.0, 0.75, clamp(afCoverageCompensation, 0.0, 1.0));
	float fCoverage = clamp(
		(fLuminance - fCoverageThreshold) /
		max(1.0 - fCoverageThreshold, 0.001), 0.0, 1.0);
	float fHeight = HeightAboveGround(avPosition);
	float fThickness = max(fCoverage * afCloudMaxHeight, 1.0);
	float fRelativeHeight = (fHeight - afCloudBaseHeight) / fThickness;

	if(fCoverage <= 0.0 || fRelativeHeight <= 0.0 || fRelativeHeight >= 1.0)
		return vec4(0.0, fCoverage, clamp(fRelativeHeight, 0.0, 1.0), fHeight);

	// Soft lower and upper boundaries hide ray-march bands. Because thickness is
	// map-driven, this also gives bright areas a visibly higher silhouette.
	float fLowerEdge = smoothstep(0.0, 0.12, fRelativeHeight);
	float fUpperEdge = 1.0 - smoothstep(0.72, 1.0, fRelativeHeight);
	float fDensity = fCoverage * fLowerEdge * fUpperEdge;
	return vec4(fDensity, fCoverage, fRelativeHeight, fHeight);
}

@ifdef AtmosphereScatteringPass
@else
float CloudOpticalDepthToSunFromGround(vec3 avGroundPosition,
	float afMipLevel)
{
	vec3 vCloudBaseRadii = avGroundRadii + vec3(afCloudBaseHeight);
	vec3 vCloudOuterRadii = avGroundRadii +
		vec3(afCloudBaseHeight + afCloudMaxHeight);
	vec2 vCloudBaseHit = RayEllipsoidIntersection(
		avGroundPosition, avSunDirection, vCloudBaseRadii);
	vec2 vCloudOuterHit = RayEllipsoidIntersection(
		avGroundPosition, avSunDirection, vCloudOuterRadii);
	float fRayStart = max(vCloudBaseHit.y, 0.0);
	float fRayEnd = vCloudOuterHit.y;

	if(fRayEnd <= fRayStart)
		return 0.0;

	float fStep = (fRayEnd - fRayStart) /
		float(kCloudShadowSampleCount);
	float fOpticalDepth = 0.0;

	for(int i = 0; i < kCloudShadowSampleCount; ++i)
	{
		float fT = fRayStart + (float(i) + 0.5) * fStep;
		vec3 vPosition = avGroundPosition + avSunDirection * fT;
		fOpticalDepth += CloudPropertiesAt(
			vPosition, afMipLevel, 0.0).x * fStep;
	}

	return fOpticalDepth;
}
@endif

float InterleavedGradientNoise(vec2 avPixelPosition)
{
	// Screen-stable sub-pixel offset that breaks coherent ray-march contours.
	return fract(52.9829189 * fract(dot(avPixelPosition,
		vec2(0.06711056, 0.00583715))));
}

void main()
{
	vec3 vRayDirection = normalize(gvLocalPosition - avCameraPosition);
	vec2 vAtmosphereHit = RayEllipsoidIntersection(
		avCameraPosition, vRayDirection, avAtmosphereRadii);

	if(vAtmosphereHit.y <= 0.0)
		discard;

	float fRayStart = max(vAtmosphereHit.x, 0.0);
	float fRayEnd = vAtmosphereHit.y;

	vec2 vGroundHit = RayEllipsoidIntersection(
		avCameraPosition, vRayDirection, avGroundRadii);
	bool bGroundVisible =
		vGroundHit.x > fRayStart && vGroundHit.x < fRayEnd;
	if(bGroundVisible)
		fRayEnd = vGroundHit.x;

	float fRayLength = fRayEnd - fRayStart;
	if(fRayLength <= 0.0)
		discard;

	float fStep = fRayLength / float(kViewSampleCount);
	vec2 vViewOpticalDepth = vec2(0.0);
	@ifdef AtmosphereScatteringPass
	vec3 vRayleighIntegral = vec3(0.0);
	vec3 vMieIntegral = vec3(0.0);
	@endif

	for(int i = 0; i < kViewSampleCount; ++i)
	{
		float fT = fRayStart + (float(i) + 0.5) * fStep;
		vec3 vPosition = avCameraPosition + vRayDirection * fT;
		vec2 vDensity = DensityAt(vPosition);
		vec2 vStepOpticalDepth = vDensity * fStep;

		@ifdef AtmosphereScatteringPass
		vec2 vSunOpticalDepth = OpticalDepthToSun(vPosition);
		if(vSunOpticalDepth.x >= 0.0)
		{
			// Treat density and solar transmission as constant over this segment,
			// then integrate the camera-path extinction analytically. Unlike a
			// midpoint estimate, this remains accurate when a dense grazing step is
			// optically thick in blue but only moderately thick in red.
			vec3 vStepExtinction = Extinction(vStepOpticalDepth);
			vec3 vViewTransmissionAtStart =
				exp(-Extinction(vViewOpticalDepth));
			vec3 vSunTransmission = exp(-Extinction(vSunOpticalDepth));
			vec3 vTransmittance = vViewTransmissionAtStart *
				vSunTransmission *
				AverageSegmentTransmittance(vStepExtinction);
			vRayleighIntegral += vTransmittance *
				(vDensity.x * fStep);
			vMieIntegral += vTransmittance * (vDensity.y * fStep);
		}
		@endif

		vViewOpticalDepth += vStepOpticalDepth;
	}

	@ifdef AtmosphereScatteringPass
	vec3 vCloudScatteredLight = vec3(0.0);
	@else
	vec3 vViewTransmittance = exp(-Extinction(vViewOpticalDepth));
	@endif
	float fCloudOpticalDepth = 0.0;

	if(afCloudEnabled > 0.5 && afCloudExtinction > 0.0)
	{
		vec3 vCloudOuterRadii = avGroundRadii +
			vec3(afCloudBaseHeight + afCloudMaxHeight);
		vec2 vCloudHit = RayEllipsoidIntersection(
			avCameraPosition, vRayDirection, vCloudOuterRadii);
		float fCloudRayStart = max(fRayStart, max(vCloudHit.x, 0.0));
		float fCloudRayEnd = min(fRayEnd, vCloudHit.y);

		// LOD is derived from the proxy surface so derivatives remain coherent
		// across the whole draw, while each volume sample supplies its own UV.
		vec2 vCloudProxyUv = CloudUv(gvLocalPosition);
		float fCloudMipLevel = GetCloudMipLevel(
			vCloudProxyUv * max(avCloudTextureLayout.xy, vec2(1.0)));

		if(fCloudRayEnd > fCloudRayStart)
		{
			// The outer proxy underestimates the cloud-map footprint where the view
			// ray becomes tangent to the much lower cloud shell. Convert that
			// geometric stretch into a smoothly capped mip bias. Explicit LOD
			// sampling remains trilinear, so this softens only truly grazing views.
			vec3 vCloudEntryPosition = avCameraPosition +
				vRayDirection * fCloudRayStart;
			vec3 vCloudEntryNormal = normalize(vCloudEntryPosition /
				(vCloudOuterRadii * vCloudOuterRadii));
			float fCloudViewCosine = abs(dot(
				-vRayDirection, vCloudEntryNormal));
			float fGrazingWeight = 1.0 - smoothstep(
				0.08, 0.35, fCloudViewCosine);
			float fGrazingMipBias = min(4.0,
				max(0.0, -log(max(fCloudViewCosine, 0.015)) *
					1.4426950408889634) * fGrazingWeight);
			float fCoverageCompensation = smoothstep(
				1.5, 4.0, fGrazingMipBias);
			fCloudMipLevel += fGrazingMipBias;

			float fCloudStep = (fCloudRayEnd - fCloudRayStart) /
				float(kCloudSampleCount);
			float fCloudJitter = InterleavedGradientNoise(gl_FragCoord.xy);
			@ifdef AtmosphereScatteringPass
			vec2 vAtmosphereOpticalDepthToCloud = vec2(0.0);
			float fPreviousAtmosphereT = fCloudRayStart;
			vAtmosphereOpticalDepthToCloud = OpticalDepthAlongView(
				avCameraPosition, vRayDirection,
				fRayStart, fCloudRayStart);
			@endif

			for(int i = 0; i < kCloudSampleCount; ++i)
			{
				float fT = fCloudRayStart +
					(float(i) + fCloudJitter) * fCloudStep;
				vec3 vPosition = avCameraPosition + vRayDirection * fT;
				@ifdef AtmosphereScatteringPass
				vec3 vAtmosphereTransmittanceToCloud = vec3(1.0);
				float fAtmosphereStep = max(
					fT - fPreviousAtmosphereT, 0.0);
				float fAtmosphereMidT = 0.5 *
					(fPreviousAtmosphereT + fT);
				vAtmosphereOpticalDepthToCloud += DensityAt(
					avCameraPosition + vRayDirection * fAtmosphereMidT) *
					fAtmosphereStep;
				vAtmosphereTransmittanceToCloud = exp(-Extinction(
					vAtmosphereOpticalDepthToCloud));
				fPreviousAtmosphereT = fT;
				@endif

				vec4 vCloudProperties = CloudPropertiesAt(
					vPosition, fCloudMipLevel, fCoverageCompensation);
				float fDensity = vCloudProperties.x;
				if(fDensity <= 0.0)
					continue;

				float fStepOpticalDepth = fDensity * fCloudStep;
				@ifdef AtmosphereScatteringPass
					float fFrontTransmittance = exp(
						-afCloudExtinction * fCloudOpticalDepth);
					float fStepOpacity = 1.0 - exp(
						-afCloudExtinction * fStepOpticalDepth);

					vec3 vGroundNormal = normalize(
						vPosition / (avGroundRadii * avGroundRadii));
					float fRawSunCosine = dot(vGroundNormal, avSunDirection);
					float fSunCosine = max(fRawSunCosine, 0.0);
					vec2 vSunAtmosphereHit = RayEllipsoidIntersection(
						vPosition, avSunDirection, avAtmosphereRadii);
					bool bShadowed = vSunAtmosphereHit.y <= 0.0 ||
						IsPlanetShadowed(vPosition, vSunAtmosphereHit.y);

					float fDirectCloudLight = 0.0;
					if(!bShadowed)
					{
						// Approximate the darkening below a cloud top without a second
						// texture ray-march toward the Sun.
						float fDepthBelowTop = 1.0 - vCloudProperties.z;
						float fSelfShadow = exp(-afCloudSelfShadow *
							vCloudProperties.y * fDepthBelowTop /
							max(fSunCosine, 0.15));
						fDirectCloudLight = afCloudSunIntensity *
							fSunCosine * fSelfShadow;
					}

					float fCoolTwilightRise = smoothstep(
						-0.104528, -0.034899, fRawSunCosine);
					float fCoolTwilightFall = 1.0 - smoothstep(
						-0.052336, 0.017452, fRawSunCosine);
					float fCoolTwilightWeight =
						fCoolTwilightRise * fCoolTwilightFall;

					float fPositiveSunCosine = max(fRawSunCosine, 0.0);
					float fAirMass = 1.0 / max(fPositiveSunCosine +
						0.025 * exp(-11.0 * fPositiveSunCosine), 0.025);
					float fCloudHeight = vCloudProperties.w;
					float fRayleighAboveCloud = exp(-fCloudHeight /
						max(afRayleighScaleHeight, 1.0));
					float fMieAboveCloud = exp(-fCloudHeight /
						max(afMieScaleHeight, 1.0));
					vec3 vVerticalOpticalDepth = avRayleighScattering *
						(afRayleighScaleHeight * fRayleighAboveCloud) +
						vec3(afMieExtinction * afAerosolDensity *
							afMieScaleHeight * fMieAboveCloud);
					vec3 vSpectralSunTransmission = exp(-vVerticalOpticalDepth *
						max(fAirMass - 1.0, 0.0));
					float fLowSunBlend = 1.0 - smoothstep(
						0.139173, 0.258819, fRawSunCosine);
					vec3 vLowSunDirectTint = mix(vec3(1.0),
						vSpectralSunTransmission, fLowSunBlend);

					float fSunsetBuild = 1.0 - smoothstep(
						0.034899, 0.207912, fRawSunCosine);
					float fSunsetMainVisibility = smoothstep(
						-0.034899, 0.008727, fRawSunCosine);
					float fSunsetTailVisibility = 0.105 * smoothstep(
						-0.043619, -0.026177, fRawSunCosine);
					float fSunsetVisibility = max(
						fSunsetMainVisibility, fSunsetTailVisibility);
					float fSunsetWeight = fSunsetBuild * fSunsetVisibility;
					vec3 vSunsetColor = vSpectralSunTransmission /
						max(vSpectralSunTransmission.r, 0.001);

					// Day-side cloud ambient follows the same spectral transmission.
					// Once the planet hides the Sun, only explicit twilight remains.
					vec3 vCloudAmbientLight = bShadowed ? vec3(0.0) :
						afCloudAmbient * mix(vec3(1.0),
							vSpectralSunTransmission, fLowSunBlend);
					vec3 vCloudIlluminance = vCloudAmbientLight +
						vLowSunDirectTint * fDirectCloudLight;
					vCloudIlluminance += avCloudTwilightColor *
						(afCloudTwilightStrength * fCoolTwilightWeight);
					vCloudIlluminance += vSunsetColor *
						(afCloudTwilightStrength * 1.75 * fSunsetWeight);
					vec3 vCloudLight = avSunColor * vCloudIlluminance;
					vec3 vCloudViewTransmission = ViewTransmissionForSolarGeometry(
						vAtmosphereTransmittanceToCloud, vPosition);
					vCloudScatteredLight += vCloudViewTransmission *
						fFrontTransmittance * fStepOpacity * vCloudLight;
				@endif

				fCloudOpticalDepth += fStepOpticalDepth;
			}
		}
	}

	@ifdef AtmosphereScatteringPass
	float fMu = dot(vRayDirection, avSunDirection);
	float fMuSquared = fMu * fMu;
	float fRayleighPhase = (3.0 / (16.0 * kPi)) * (1.0 + fMuSquared);

	float fG = afMieAnisotropy;
	float fG2 = fG * fG;
	float fMieDenominator = max(1.0 + fG2 - 2.0 * fG * fMu, 0.0001);
	float fMiePhase = (3.0 / (8.0 * kPi)) *
		((1.0 - fG2) * (1.0 + fMuSquared)) /
		((2.0 + fG2) * pow(fMieDenominator, 1.5));

	vec3 vScatteringSource =
		avRayleighScattering * vRayleighIntegral +
		vec3(afMieScattering) * vMieIntegral;
	vec3 vSingleScatteredLight =
		avRayleighScattering * vRayleighIntegral * fRayleighPhase +
		vec3(afMieScattering) * vMieIntegral * fMiePhase;

	// Reuse the physically integrated, Sun-visible scattering source as a
	// low-order isotropic approximation of light redirected by additional
	// atmospheric events. It therefore grows with optical path length and fades
	// in planetary shadow instead of becoming a uniform screen-space fog.
	float fIsotropicPhase = 1.0 / (4.0 * kPi);
	vec3 vMultipleScatteredLight = vScatteringSource *
		(fIsotropicPhase * afMultipleScatteringStrength);
	vec3 vScatteredLight = avSunColor * afExposure *
		(vSingleScatteredLight + vMultipleScatteredLight) +
		vCloudScatteredLight;

	// Additive pass. Alpha remains nonzero so HPL's translucent alpha test keeps
	// the fragment; blend mode affects RGB independently of that test.
	gl_FragColor = vec4(vScatteredLight, 1.0);
	@else
	float fCloudTransmittance = exp(
		-afCloudExtinction * fCloudOpticalDepth);
	vViewTransmittance *= fCloudTransmittance;

	if(bGroundVisible && afCloudEnabled > 0.5 &&
		afCloudShadowStrength > 0.0 && afCloudExtinction > 0.0)
	{
		vec3 vGroundPosition =
			avCameraPosition + vRayDirection * vGroundHit.x;
		vec3 vGroundNormal = normalize(
			vGroundPosition / (avGroundRadii * avGroundRadii));
		float fSunCosine = dot(vGroundNormal, avSunDirection);

		if(fSunCosine > 0.0 &&
			dot(avSunColor, vec3(0.2126, 0.7152, 0.0722)) > 0.0)
		{
			vec2 vGroundUv = CloudUv(vGroundPosition);
			float fShadowMipLevel = GetCloudMipLevel(
				vGroundUv * max(avCloudTextureLayout.xy, vec2(1.0))) +
				afCloudShadowSoftness;
			float fShadowOpticalDepth = CloudOpticalDepthToSunFromGround(
				vGroundPosition, fShadowMipLevel);
			float fCloudShadow = exp(
				-afCloudExtinction * fShadowOpticalDepth);
			// Fade the effect close to the terminator, where the deferred surface
			// has little direct sunlight left to shadow and tangent paths become
			// disproportionately long.
			float fDirectLightWeight = smoothstep(0.02, 0.12, fSunCosine);
			float fShadowWeight = clamp(
				afCloudShadowStrength * fDirectLightWeight, 0.0, 1.0);
			vViewTransmittance *= mix(1.0, fCloudShadow, fShadowWeight);
		}
	}

	float fChromaAnchorT = bGroundVisible ? vGroundHit.x :
		clamp(-dot(avCameraPosition, vRayDirection), fRayStart, fRayEnd);
	vec3 vChromaAnchor = avCameraPosition + vRayDirection * fChromaAnchorT;
	vViewTransmittance = ViewTransmissionForSolarGeometry(
		vViewTransmittance, vChromaAnchor);

	// Multiplicative pass: view-path extinction dims at all angles, while its
	// chromatic shift is gated by the actual local solar terminator.
	gl_FragColor = vec4(vViewTransmittance, 1.0);
	@endif
}
