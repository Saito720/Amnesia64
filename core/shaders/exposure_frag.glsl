#version 120
#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect aScene;
@define sampler_aScene 0

uniform float afExposure;

void main() {
	vec3 c = texture2DRect(aScene, gl_FragCoord.xy).rgb;
	c = max(c, vec3(0.0));
	c *= afExposure;
	gl_FragColor = vec4(c, 1.0);
}
