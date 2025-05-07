#version 440
/// Copyright © 2009-2020 Frictional Games
/// Copyright 2023 Michael Pollind
/// SPDX-License-Identifier: GPL-3.0
#include "gui.resource.glsl"

layout(location = 0) in vec2 v_texcoord;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec4 v_position;

layout(location = 2) out vec4 out_color;

void main(void)
{
    if(HasClipPlanes(pass.textureConfig))
    {
	    for(int i = 0; i < 4; i++)
	    {
	        float d = dot(pass.clipPlane[i], vec4(v_position.xyz, 1.0));
	        if(d < 0.0)
	        {
		        discard;
	        }
	    }
    }

    out_color = v_color;
    if(HasDiffuse(pass.textureConfig))
    {
       out_color *= texture(sampler2D(diffuseMap, diffuseSampler), v_texcoord); 
    }

}

