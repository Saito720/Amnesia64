
#ifndef HPL_TEXTURE_H
#define HPL_TEXTURE_H


#include "graphics/RIFormat.h"
#include <graphics/Bitmap.h>

#include <graphics/RITypes.h>
#include <graphics/GraphicsTypes.h>
#include <graphics/RIResourceUploader.h>
#include <graphics/RIScratchAlloc.h>

#include <array>
#include <memory>

struct RIResourceUploader_s;

namespace hpl {
struct RIBoostrap;
  
RI_Format to_image_supported_format(ePixelFormat format); 

struct HPLTexture {
  struct RIBoostrap *bootstrap;
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
  //struct RIDescriptor_s *samplerBinding;
  
  //HPLTexture(HPLTexture&&) = delete;
  //HPLTexture(const HPLTexture&) = delete;

  static void HPLTexture_Delete(HPLTexture* tex); 

  struct BitmapLoadOptions {
  public:
    bool use_cubemap = false;
    bool use_array = false;
    bool use_mipmaps = false;
  };
  bool LoadBitmap(struct RIBoostrap *bootstrap,
                  RIBarrierImageHandle_s postBarrier, 
                  cBitmap &bitmap,
                  const BitmapLoadOptions &options);
  // HPLTexture();
  // HPLTexture(struct RIResourceUploader_s *upload, cBitmap &bitmap,
  //            const BitmapLoadOptions &options);
};

} // namespace hpl

#endif
