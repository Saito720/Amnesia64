#ifndef BINDLESS_RESOURCE_GLSL
#define BINDLESS_RESOURCE_GLSL

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference     : require

layout(buffer_reference) buffer Position;

// Engine-wide bindless texture set. Populated by the RIBootstrap registry;
// any shader that includes this file gets the same slot ids.
layout (set = 0, binding = 0) uniform sampler2D textures_2d[];
layout (set = 0, binding = 1) uniform samplerCube textures_cube[];
layout (set = 0, binding = 2) uniform sampler2DArray textures_2d_array[];

#endif // BINDLESS_RESOURCE_GLSL

