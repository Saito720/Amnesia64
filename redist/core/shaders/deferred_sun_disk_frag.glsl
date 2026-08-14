////////////////////////////////////////////////////////
// Deferred Astronomical Sun Disk - Fragment Shader
////////////////////////////////////////////////////////
#version 120
#extension GL_ARB_texture_rectangle : enable

varying vec3 gvFarPlanePos;

uniform vec3 avSunDirection;
uniform vec4 avSunDiskColor;
uniform float afSunDiskCosRadius;

uniform sampler2DRect aDepthMap;
@define sampler_aDepthMap 0

void main()
{
	// The disk is part of the sky and must remain behind all scene geometry.
	vec3 vDepth = texture2DRect(aDepthMap, gl_FragCoord.xy).xyz;
	if(dot(vDepth, vDepth) != 0.0) discard;

	vec3 vViewDirection = normalize(gvFarPlanePos);
	float fAlignment = dot(vViewDirection, normalize(avSunDirection));
	float fEdgeWidth = max(fwidth(fAlignment), 0.0000001);
	float fCoverage = smoothstep(afSunDiskCosRadius - fEdgeWidth,
		afSunDiskCosRadius + fEdgeWidth, fAlignment);
	if(fCoverage <= 0.0) discard;

	gl_FragColor = vec4(avSunDiskColor.rgb * fCoverage, fCoverage);
}
