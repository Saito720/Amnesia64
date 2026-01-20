#include "graphics/JpegFrameCrunch.h"
#include "src/turbojpeg.h"

cJpegFrameCrunch::cJpegFrameCrunch()
{
    mpComp = NULL;
    mpDecomp = NULL;
    mpJpegBuf = NULL;
    mlJpegSize = 0;
}

cJpegFrameCrunch::~cJpegFrameCrunch()
{
    TJFreeSafe(mpJpegBuf);
    mlJpegSize = 0;

    if (mpComp) { tjDestroy(mpComp);   mpComp = NULL; }
    if (mpDecomp) { tjDestroy(mpDecomp); mpDecomp = NULL; }

    mvInRGBA.clear();
    mvOutRGBA.clear();
}

bool cJpegFrameCrunch::Init()
{
    if (!mpComp)  mpComp = tjInitCompress();
    if (!mpDecomp) mpDecomp = tjInitDecompress();
    return (mpComp != NULL && mpDecomp != NULL);
}

void cJpegFrameCrunch::TJFreeSafe(unsigned char*& p)
{
    if (p) { tjFree(p); p = NULL; }
}

const uint8_t* cJpegFrameCrunch::CrunchFromTexture(GLenum target, GLuint srcTex, int& outW, int& outH, int quality)
{
    if (!Init()) return NULL;
    if (srcTex == 0) return NULL;
    if (!glIsTexture(srcTex)) return NULL;

    quality = (quality < 1) ? 1 : (quality > 100) ? 100 : quality;

    glBindTexture(target, srcTex);

    GLint texW = 0, texH = 0;
    glGetTexLevelParameteriv(target, 0, GL_TEXTURE_WIDTH, &texW);
    glGetTexLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, &texH);
    if (texW <= 0 || texH <= 0) return NULL;

    outW = texW;
    outH = texH;

    mvInRGBA.resize((size_t)texW * (size_t)texH * 4);
    mvOutRGBA.resize((size_t)texW * (size_t)texH * 4);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(target, 0, GL_RGBA, GL_UNSIGNED_BYTE, mvInRGBA.data());

    TJFreeSafe(mpJpegBuf);
    mlJpegSize = 0;

    const int subsamp = TJSAMP_420;
    const int flags = TJFLAG_FASTDCT | TJFLAG_BOTTOMUP;

    if (tjCompress2(mpComp, mvInRGBA.data(), texW, 0, texH, TJPF_RGBA,
        &mpJpegBuf, &mlJpegSize, subsamp, quality, flags) != 0)
        return NULL;

    if (tjDecompress2(mpDecomp, mpJpegBuf, mlJpegSize,
        mvOutRGBA.data(), texW, 0, texH, TJPF_RGBA, flags) != 0)
        return NULL;

    // JPEG has no alpha -> force opaque
    const size_t pixels = (size_t)texW * (size_t)texH;
    for (size_t i = 0; i < pixels; ++i)
        mvOutRGBA[i * 4 + 3] = 255;

    return mvOutRGBA.data();
}
