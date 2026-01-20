#ifndef HPL_JPEG_FRAME_CRUNCH_H
#define HPL_JPEG_FRAME_CRUNCH_H

#include <vector>
#include <cstdint>
#include <GL/glew.h>
#include "src/turbojpeg.h"

class cJpegFrameCrunch
{
public:
    cJpegFrameCrunch();
	~cJpegFrameCrunch();

    bool Init();

    void TJFreeSafe(unsigned char*& p);

    const uint8_t* CrunchFromTexture(GLenum target, GLuint srcTex, int& outW, int& outH, int quality);

private:
    tjhandle mpComp;
    tjhandle mpDecomp;

    std::vector<uint8_t> mvInRGBA;
    std::vector<uint8_t> mvOutRGBA;

    unsigned char* mpJpegBuf;
    unsigned long  mlJpegSize;
};

#endif // HPL_JPEG_FRAME_CRUNCH_H