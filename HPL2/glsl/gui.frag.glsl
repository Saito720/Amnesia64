/// Copyright © 2009-2020 Frictional Games
/// Copyright 2023 Michael Pollind
/// SPDX-License-Identifier: GPL-3.0
#include "gui.resource.glsl"

layout(location = 0) out vec4 position;
layout(location = 1) out vec4 texcoord;
layout(location = 2) out vec4 color;

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_texcoord;
layout(location = 2) in vec4 a_color; 

void main(void)
{
    if(HasClipPlanes(pass.textureConfig))
    {
	    for(int i = 0; i < 4; i++)
	    {
	        float d = dot(pass.clipPlane[i], vec4(pos, 1.0));
	        if(d < 0.0)
	        {
		        discard;
	        }
	    }
    }

    vec4 color = color;      // Get the input color.
    if(HasDiffuse(pass.textureConfig))
    {
       color *= SampleTex2D(diffuseMap, diffuseSampler, texcoord); // Multiply the color by the texture color.
    }
	  color = vec4(texture(sampler2D(u_BaseTexture, u_BaseSampler), v_TexCoord));
    RETURN(color);
}

