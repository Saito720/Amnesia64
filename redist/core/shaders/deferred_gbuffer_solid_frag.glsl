////////////////////////////////////////////////////////
// Deferred G-Buffer Solid - Fragment Shader
//
//
////////////////////////////////////////////////////////
#version 120
#extension GL_ARB_texture_rectangle : enable
@ifdef UseDiffuseArray
#extension GL_EXT_texture_array : enable
#extension GL_ARB_shader_texture_lod : enable
@endif

@include helper_float_packing.glsl
@include helper_reflection.glsl

////////////////////
//Normal interpolated values
varying vec3 gvNormal;

@ifdef UseNormalMapping
	varying vec3 gvTangent;
	varying vec3 gvBinormal;
@endif

/////////////////////
//Extra interpolated values

//32 bit G-Buffer
@ifdef Deferred_32bit
	varying float gfLinearDepth;
@endif

//64 bit G-Buffer
@ifdef Deferred_64bit || UseEnvMap
	varying vec3 gvVertexPos;
@endif

////////////////////
//Textures
@ifdef UseDiffuseArray
uniform sampler2DArray aDiffuseMap;
uniform vec2 avDiffuseArrayTileGrid;
uniform vec2 avDiffuseArrayTileSize;
@else
uniform sampler2D aDiffuseMap;
@endif
@define sampler_aDiffuseMap 0

@ifdef UseNormalMapping
	uniform sampler2D aNormalMap;
	@define sampler_aNormalMap 1
@endif

@ifdef UseSpecular
	uniform sampler2D aSpecularMap;
	@define sampler_aSpecularMap 2
@endif

@ifdef UseParallax
	uniform sampler2D aHeightMap;
	@define sampler_aHeightMap 3

	uniform vec2 avHeightMapScaleAndBias;

	varying vec3 gvTangentEyePos;
@endif

@ifdef UseEnvMap
	uniform samplerCube aEnvMap;
	@define sampler_aEnvMap 4

	uniform vec2 avFrenselBiasPow;
	uniform mat4 a_mtxInvViewRotation;
@endif

@ifdef UseCubeMapAlpha
	uniform sampler2D aEnvMapAlphaMap;
	@define sampler_aEnvMapAlphaMap 5

@endif


//--------------------------------------------------

@ifdef UseDiffuseArray
float GetDiffuseArrayMipLevel(vec2 avScaledTexCoord)
{
	vec2 vTileSize = max(avDiffuseArrayTileSize, vec2(1.0));
	vec2 vDx = dFdx(avScaledTexCoord) * vTileSize;
	vec2 vDy = dFdy(avScaledTexCoord) * vTileSize;
	float fRho2 = max(dot(vDx, vDx), dot(vDy, vDy));
	return max(0.0, 0.5 * log(max(fRho2, 1.0)) * 1.4426950408889634);
}

vec4 SampleDiffuseMap(vec2 avTexCoord)
{
	vec2 vTileGrid = max(avDiffuseArrayTileGrid, vec2(1.0));
	vec2 vWrappedCoord = vec2(fract(avTexCoord.x), clamp(avTexCoord.y, 0.0, 0.999999));
	vec2 vScaledCoord = vWrappedCoord * vTileGrid;
	vec2 vTile = floor(vScaledCoord);
	vec2 vLocalCoord = vScaledCoord - vTile;
	float fMipLevel = GetDiffuseArrayMipLevel(avTexCoord * vTileGrid);
	float fLayer = (vTileGrid.y - 1.0 - vTile.y) * vTileGrid.x + vTile.x;
	return texture2DArrayLod(aDiffuseMap, vec3(vLocalCoord, fLayer), fMipLevel);
}
@else
vec4 SampleDiffuseMap(vec2 avTexCoord)
{
	return texture2D(aDiffuseMap, avTexCoord);
}
@endif

//--------------------------------------------------

///////////////////////////////
// Relief mapping helpers

//Optimize by getting 4 depths at once somehow?
void RayLinearIntersectionSM2(sampler2D aHeightMap, inout vec3 avPosition, inout vec3 avEyeVec)
{
	const int lSearchSteps = 8;

	avEyeVec /= lSearchSteps; 	//Split up the eye vector into the number of steps

	for(int i=0; i<lSearchSteps-1; i++)
	{
		float fDepth = texture2D(aHeightMap, avPosition.xy).w;
		if(avPosition.z < fDepth) avPosition += avEyeVec;
	}
}

void RayLinearIntersectionSM3(sampler2D aHeightMap, float afSearchSteps, inout vec3 avPosition, inout vec3 avEyeVec)
{
	avEyeVec /= afSearchSteps; 	//Split up the eye vector into the number of steps

	for(int i=0; i<afSearchSteps-1; i++)
	{
		float fDepth = texture2D(aHeightMap, avPosition.xy).w;
		if(avPosition.z < fDepth) avPosition += avEyeVec;
	}
}


void RayBinaryIntersection(sampler2D aHeightMap, inout vec3 avPosition, inout vec3 avEyeVec)
{
	const int lSearchSteps = 6;
	for(int i=0; i<lSearchSteps; i++)
	{
		float fDepth = texture2D(aHeightMap, avPosition.xy).w;
		if(avPosition.z < fDepth)
			avPosition += avEyeVec;

		avEyeVec *= 0.5;
		avPosition -= avEyeVec;
	}
}



//--------------------------------------------------

///////////////////////////////
// Main program
void main()
{
	//////////////////////////////////
	//Diffuse
	@ifdef UseParallax

		//RELIEF PARALLAX:
		@ifdef ParallaxMethod_Relief
			vec3 vEyeVec = normalize(gvTangentEyePos);

			//Get give eyevec the length so it reaches bottom.
			vEyeVec *= vec3(1.0 / vEyeVec.z);

			//Apply scale and bias
			vEyeVec.xy *= avHeightMapScaleAndBias.x;
			//vec2 vBiasPosOffset = vEyeVec.xy * avHeightMapScaleAndBias.y; <- not working! because the ray casting buggers out when u are really close!

			//Get the postion to start the search from. 0 since we start at the top.
			vec3 vHeightMapPos = vec3(gl_TexCoord[0].xy, 0.0);

			//Determine number of steps based on angle between normal and eye
			float fSteps = floor(( 1 - dot(vEyeVec, normalize(gvNormal)) ) * 18.0) + 2.0;

			//Do a linear search to find the first intersection.
			RayLinearIntersectionSM3(aHeightMap,fSteps, vHeightMapPos,  vEyeVec);

			//Do a binary search between start and first intersection to pinpoint the postion.
			RayBinaryIntersection(aHeightMap,vHeightMapPos,  vEyeVec);

			vec2 vTexCoord = vHeightMapPos.xy;
		@else
			vec3 vEyeVec = normalize(gvTangentEyePos);
			float fHeight = texture2D(aHeightMap, gl_TexCoord[0].xy).w;

			float fDisplacement = fHeight * avHeightMapScaleAndBias.x;// + avHeightMapScaleAndBias.y; <- skip bias, since relief does not support it!

			vec2 vTexCoord = (vEyeVec * fDisplacement + gl_TexCoord[0].xyz).xy;
		@endif

		vec4 vDiffuseColor = SampleDiffuseMap(vTexCoord);

		//gl_FragData[0] = vec4(0,0,0,1);
		//gl_FragData[0].xyz = vec3(vHeightMapPos.z);
		//gl_FragData[0].xyz = vec3(texture2D(aHeightMap, gl_TexCoord[0].xy).w);
	@else
		vec2 vTexCoord = gl_TexCoord[0].xy;
		vec4 vDiffuseColor = SampleDiffuseMap(vTexCoord);
	@endif


	//////////////////////////////////
	//Set Diffuse color if no environemnt mapping is used.
	@ifdef UseUnlit
		gl_FragData[0] = vec4(0.0, 0.0, 0.0, vDiffuseColor.a);
	@else
	@ifdef UseEnvMap
	@else
		gl_FragData[0] = vDiffuseColor;
	@endif
	@endif

	//////////////////////////////////
	//Normal
	@ifdef UseNormalMapping
		vec3 vNormal = texture2D(aNormalMap,vTexCoord).xyz - 0.5; //No need for full unpack x*2-1, becuase normal is normalized. (but now we do not normalize...)
		@ifdef UseEllipsoidNormals
			vec3 vBaseNormal = normalize(gvNormal);
			vec3 vTangent = normalize(gvTangent - vBaseNormal * dot(gvTangent, vBaseNormal));
			vec3 vBinormal = normalize(cross(vBaseNormal, vTangent));
			vBinormal *= dot(vBinormal, gvBinormal) < 0.0 ? -1.0 : 1.0;
			vec3 vScreenNormal = normalize(vNormal.x * vTangent + vNormal.y * vBinormal + vNormal.z * vBaseNormal);
		@else
			vec3 vScreenNormal = normalize(vNormal.x * gvTangent + vNormal.y * gvBinormal + vNormal.z * gvNormal);
		@endif
	@else
		vec3 vScreenNormal = normalize(gvNormal);
	@endif
	//If 32 bit we need to pack, else no packing is needed.
	@ifdef Deferred_32bit
		gl_FragData[1].xyz = vScreenNormal*0.5 + 0.5;
	@elseif Deferred_64bit
		gl_FragData[1].xyz = vScreenNormal;
	@endif


	//////////////////////////////////
	//Environment Map
	@ifdef UseUnlit
	@else
	@ifdef UseEnvMap
		vec3 vCameraSpaceEyeVec = normalize(gvVertexPos);

		vec3 vEnvUv = reflect(vCameraSpaceEyeVec, vScreenNormal);
		vEnvUv = (a_mtxInvViewRotation * vec4(vEnvUv,1)).xyz;

		vec4 vReflectionColor = textureCube(aEnvMap, vEnvUv);

		float afEDotN = max(dot(-vCameraSpaceEyeVec, vScreenNormal),0.0);
		float fFresnel = Fresnel(afEDotN, avFrenselBiasPow.x, avFrenselBiasPow.y);

		@ifdef UseCubeMapAlpha
			float fEnvMapAlpha = texture2D(aEnvMapAlphaMap, vTexCoord).w;
			vReflectionColor *= fEnvMapAlpha;
		@endif

		gl_FragData[0] = vDiffuseColor + vReflectionColor * fFresnel;
	@endif
	@endif


	//////////////////////////////////
	//Depth
	@ifdef Deferred_32bit
		gl_FragData[2].xyz = PackFloatInVec3(gfLinearDepth);
	@elseif Deferred_64bit
		gl_FragData[2].xyz = gvVertexPos;

		//Virtual postion offset. Use avHeightMapScaleAndBias here instead of VirtualPositionAddScale? or will that cause shadow artifacts?
		@ifdef UseParallax
			//Skip this for now!
			//gl_FragData[2].xyz += vScreenNormal * fHeight * $VirtualPositionAddScale;
		@endif
	@endif

	//////////////////////////////////
	//Specular
	@ifdef RenderTargets_4
		@ifdef UseUnlit
			gl_FragData[3].xy = vec2(0.0);
		@else
		@ifdef UseSpecular
			gl_FragData[3].xy = texture2D(aSpecularMap, vTexCoord).xy;
		@else
			gl_FragData[3].xy = vec2(0.0);
		@endif
		@endif
	@else
		@ifdef UseUnlit
			gl_FragData[1].w = 0.0;
			gl_FragData[2].w = 0.0;
		@else
		@ifdef UseSpecular
			vec2 vSpecVals = texture2D(aSpecularMap, vTexCoord).xy;
			gl_FragData[1].w = vSpecVals.x;
			gl_FragData[2].w = vSpecVals.y;
		@else
			gl_FragData[1].w = 0.0;
			gl_FragData[2].w = 0.0;
		@endif
		@endif
	@endif
}
