
layout(set = 0, binding = 0) uniform uniformBlock {
    mat4 mvp;
    vec4 clipPlane[4];
    int textureConfig;
} pass;

layout(set = 0, binding = 1) uniform sampler diffuseSampler;
layout(set = 0, binding = 2) uniform texture2D diffuseMap;

bool HasDiffuse(int _textureConfig)        { return (_textureConfig & (1 << 0)) != 0; }
bool HasClipPlanes(int _textureConfig)        { return (_textureConfig & (1 << 1)) != 0; }


