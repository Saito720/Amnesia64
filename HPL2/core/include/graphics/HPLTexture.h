
#ifndef HPL_TEXTURE__H__
#define HPL_TEXTURE__H__

#include "graphics/GraphicsTypes.h"
#include "graphics/RIFormat.h"
#include "graphics/RITypes.h"
#include <array>
#include <memory>

struct RIResourceUploader_s;

namespace hpl {
struct RIBoostrap;
class cBitmap;

RI_Format to_image_supported_format(ePixelFormat format); 

struct HPLTexture {
public:
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
