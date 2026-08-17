////////////////////////////////////////////////////////
// Deferred Solid Sky - Fragment Shader
////////////////////////////////////////////////////////
#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect aDepthMap;
@define sampler_aDepthMap 0

uniform vec4 avSkyColor;

void main()
{
	vec3 vDepth = texture2DRect(aDepthMap, gl_FragCoord.xy).xyz;
	if(dot(vDepth, vDepth) != 0.0) discard;

	gl_FragColor = avSkyColor;
}
