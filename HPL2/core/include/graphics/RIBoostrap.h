
#ifndef HPL_GRAPHICS_BACKEND_H
#define HPL_GRAPHICS_BACKEND_H

#include "graphics/GraphicsTypes.h"
#include "graphics/RITypes.h"
#include <array>
#include <graphics/RIResourceUploader.h>
#include <graphics/RIScratchAlloc.h>

namespace hpl {

//bootstrap implementation
struct RIBoostrap {
  struct FrameContext {
    struct RIScratchAlloc_s ubo_scratch;
    struct RICmd_s cmd;
    union {
#if (DEVICE_IMPL_VULKAN)
      struct {
        VkCommandPool pool;
      } vk;
#endif
    };
  };

	union {
#if ( DEVICE_IMPL_VULKAN )
		struct {
			VkSemaphore frame_sem;	
		} vk;
#endif
	};
  RIRenderer_s renderer;
  RIDevice_s device;
  RISwapchain_s swapchain;

  std::array<FrameContext, RI_NUMBER_FRAMES_FLIGHT> frame_sets;
	std::array<RIDescriptor_s, 2048> cached_filters; 
  uint32_t swapchain_index;
  uint64_t frame_count = 0;

  struct RIResourceUploader_s uploader = {};

  FrameContext *GetActiveSet() {
    return &frame_sets[frame_count % RI_NUMBER_FRAMES_FLIGHT];
  }
  RIDescriptor_s *resolve_filter_descriptor(eTextureWrap wrapS, eTextureWrap wrapT, eTextureWrap wrapR, eTextureFilter filter);

};


}; // namespace hpl

#endif
