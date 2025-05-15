
#ifndef HPL_GRAPHICS_BACKEND_H
#define HPL_GRAPHICS_BACKEND_H

#include "graphics/GraphicsTypes.h"
#include "graphics/RISegmentAlloc.h"
#include "graphics/RITypes.h"
#include <array>
#include <graphics/RIResourceUploader.h>
#include <graphics/RIScratchAlloc.h>

namespace hpl {

//bootstrap implementation
struct RIBoostrap {
public:
  explicit RIBoostrap() {

  }
  struct FrameContext {
    struct RIScratchAlloc_s UBOScratchAlloc;
    struct RICmd_s cmd;
    std::vector<RIFree_s> Freelist;
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

  struct RISegmentAlloc<RI_NUMBER_FRAMES_FLIGHT> GUIVertexAlloc;
  RIBuffer_s guiVertexBuffer; 
  struct RISegmentAlloc<RI_NUMBER_FRAMES_FLIGHT> GUIIndexAlloc;
  RIBuffer_s guiIndexBuffer;

  std::array<FrameContext, RI_NUMBER_FRAMES_FLIGHT> frame_sets;
	std::array<RIDescriptor_s, 2048> cachedFilters; 
  uint32_t swapchain_index;
  uint64_t frame_count = 0;

  struct RIResourceUploader_s uploader = {};

  void IncrementFrame();
  RIDescriptor_s *resolve_filter_descriptor(eTextureWrap wrapS, eTextureWrap wrapT, eTextureWrap wrapR, eTextureFilter filter);
  FrameContext *GetActiveSet() { return &frame_sets[frame_count % RI_NUMBER_FRAMES_FLIGHT]; }

  void UpdateFrameUBO(RIDescriptor_s* descriptor, void* data, size_t size);

};
extern struct RIBoostrap RI; 

}; // namespace hpl

#endif
