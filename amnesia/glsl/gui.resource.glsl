layout(set = 0, binding = 0) uniform uniformBlock {
    mat4 mvp;
    vec4 clipPlane[4];
    int textureConfig;
} pass;

bool HasDiffuse(int _textureConfig)        { return (_textureConfig & (1 << 0)) != 0; }
bool HasClipPlanes(int _textureConfig)        { return (_textureConfig & (1 << 1)) != 0; }


