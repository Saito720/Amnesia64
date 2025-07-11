
#ifndef HPL_TEXTURE__H__
#define HPL_TEXTURE__H__

#include "graphics/GraphicsTypes.h"
#include "graphics/RIFormat.h"
#include "graphics/RITypes.h"

struct RIResourceUploader_s;

namespace hpl {
struct RIBootstrap;
class cBitmap;

RI_Format to_image_supported_format(ePixelFormat format); 

struct HPLTexture {
public:
  struct RITexture_s handle;
  union {
#if (DEVICE_IMPL_VULKAN)
    struct {
      struct VmaAllocation_T *vmaAlloc;
    } vk;
#endif
  };
  uint16_t width;
  uint16_t height;
  uint16_t depth;
  uint8_t mipNum;
  struct RIDescriptor_s binding;

  static void HPLTexture_Delete(HPLTexture* tex); 

  struct BitmapLoadOptions {
  public:
    bool use_cubemap = false;
    bool use_array = false;
    bool use_mipmaps = false;
  };
  bool LoadBitmap(RIBarrierImageHandle_s postBarrier, 
                  cBitmap &bitmap,
                  const BitmapLoadOptions &options);
  void setDebugName(const tWString& name);
  void setDebugName(const char* name);
};

} // namespace hpl

#endif
