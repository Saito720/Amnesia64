////////////////////////////////////////////////////////
// Deferred Scene Exposure - Fragment Shader
////////////////////////////////////////////////////////
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect aSceneMap;
@define sampler_aSceneMap 0

uniform sampler2DRect aDepthMap;
@define sampler_aDepthMap 1

uniform float afExposure;
uniform float afPreserveSolidSky;
uniform vec4 avPreservedSkyColor;

void main()
{
	vec4 vSource = texture2DRect(aSceneMap, gl_FragCoord.xy);

	// A textureless sky is the user's chosen background color, not scene
	// radiance. Keep untouched empty pixels at exactly that authored value.
	vec3 vDepth = texture2DRect(aDepthMap, gl_FragCoord.xy).xyz;
	vec3 vSkyDifference = abs(vSource.rgb - avPreservedSkyColor.rgb);
	if(afPreserveSolidSky > 0.5 && dot(vDepth, vDepth) == 0.0 &&
		max(vSkyDifference.r, max(vSkyDifference.g, vSkyDifference.b)) < (0.5 / 255.0))
	{
		gl_FragColor = vSource;
		return;
	}

	// This rational curve behaves like a small exposure increase in the
	// shadows and midtones while smoothly protecting highlights. It is the
	// identity at 1.0 and keeps display white fixed at white.
	float fExposure = max(afExposure, 0.0001);
	vec3 vDenominator = max(vec3(0.0001), vec3(1.0) + vSource.rgb * (fExposure - 1.0));
	vec3 vExposed = (vSource.rgb * fExposure) / vDenominator;

	gl_FragColor = vec4(vExposed, vSource.a);
}
