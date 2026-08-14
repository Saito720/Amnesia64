////////////////////////////////////////////////////////
// Deferred Illumination - Fragment Shader
//
// Used in a sepperate pass to render illuminating parts of a material.
////////////////////////////////////////////////////////
#version 120

@ifdef UseDiffuseArray
#extension GL_EXT_texture_array : enable
#extension GL_ARB_shader_texture_lod : enable
uniform sampler2DArray aDiffuse;
uniform vec2 avDiffuseArrayTileGrid;
uniform vec2 avDiffuseArrayTileSize;
@else
uniform sampler2D aDiffuse;
@endif
@define sampler_aDiffuse 0

uniform float afColorMul;

@ifdef UseDiffuseArray
float GetDiffuseArrayMipLevel(vec2 avScaledTexCoord)
{
	vec2 vTileSize = max(avDiffuseArrayTileSize, vec2(1.0));
	vec2 vDx = dFdx(avScaledTexCoord) * vTileSize;
	vec2 vDy = dFdy(avScaledTexCoord) * vTileSize;
	float fRho2 = max(dot(vDx, vDx), dot(vDy, vDy));
	return max(0.0, 0.5 * log(max(fRho2, 1.0)) * 1.4426950408889634);
}

vec4 SampleDiffuse(vec2 avTexCoord)
{
	vec2 vTileGrid = max(avDiffuseArrayTileGrid, vec2(1.0));
	vec2 vWrappedCoord = vec2(fract(avTexCoord.x), clamp(avTexCoord.y, 0.0, 0.999999));
	vec2 vScaledCoord = vWrappedCoord * vTileGrid;
	vec2 vTile = floor(vScaledCoord);
	vec2 vLocalCoord = vScaledCoord - vTile;
	float fMipLevel = GetDiffuseArrayMipLevel(avTexCoord * vTileGrid);
	float fLayer = (vTileGrid.y - 1.0 - vTile.y) * vTileGrid.x + vTile.x;
	return texture2DArrayLod(aDiffuse, vec3(vLocalCoord, fLayer), fMipLevel);
}
@else
vec4 SampleDiffuse(vec2 avTexCoord)
{
	return texture2D(aDiffuse, avTexCoord);
}
@endif

void main()
{
	gl_FragColor = SampleDiffuse(gl_TexCoord[0].xy) * afColorMul;
}
