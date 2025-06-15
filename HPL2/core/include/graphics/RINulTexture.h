#ifndef RI_NUL_TEXTURE_H
#define RI_NUL_TEXTURE_H

#include "graphics/RITypes.h"

struct RINulTexture{
  struct RITexture_s tex;
  struct RIDescriptor_s binding;
  union {
#if (DEVICE_IMPL_VULKAN)
    struct {
      struct VmaAllocation_T *vmaAlloc;
    } vk;
#endif
  };

  static bool Create2DNulWhite(struct RICmd_s* cmd, struct RIDevice_s* device, struct RINulTexture* tex);
};

#endif
