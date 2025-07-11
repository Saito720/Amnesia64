#version 440
/// Copyright © 2009-2020 Frictional Games
/// Copyright 2023 Michael Pollind
/// SPDX-License-Identifier: GPL-3.0
#include "gui.resource.glsl"

layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec4 v_position;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 2) in vec4 a_color; 

void main(void)
{
    v_color = a_color;
    v_texcoord = a_texcoord;
    vec4 pos = pass.mvp * vec4(a_position, 1.0);
    v_position = pos;
    gl_Position = pos;
}
