
#ifndef HPL_HANDLE_H
#define HPL_HANDLE_H


#include <graphics/Bitmap.h>

#include <graphics/RITypes.h>
#include <graphics/GraphicsTypes.h>
#include <graphics/RIResourceUploader.h>
#include <graphics/RIScratchAlloc.h>

#include <array>

namespace hpl {
struct RIBoostrap;
struct RIResourceUploader_s;

struct HPLTexture {
  struct BitmapLoadOptions {
  public:
      bool m_useCubeMap = false;
      bool m_useArray = false;
      bool m_useMipmaps = false;
  };

  struct RIBoostrap *bootstrap;
  struct RITexture_s handle;
  union {
#if (DEVICE_IMPL_VULKAN)
    struct {
      struct VmaAllocation_T *vmaAlloc;
    } vk;
#endif
  };
  uint8_t mipNum;
  struct RIDescriptor_s binding;
  struct RIDescriptor_s *samplerBinding;

  struct HPLTexture_Delete {
    void operator()(HPLTexture *texture);
  };

  HPLTexture(); 
  HPLTexture(struct RIResourceUploader_s* upload,cBitmap& bitmap, const BitmapLoadOptions& options);

};

} // namespace hpl

#endif
