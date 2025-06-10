
#ifndef HPL_GRAPHICS_BACKEND_H
#define HPL_GRAPHICS_BACKEND_H

#include "graphics/GraphicsTypes.h"
#include "graphics/RISegmentAlloc.h"
#include "graphics/RITypes.h"
#include <array>

#include <graphics/RIResourceUploader.h>
#include <graphics/RIScratchAlloc.h>
#include <graphics/RIProgram.h>

namespace hpl {

//bootstrap implementation
struct RIBoostrap {
public:
  explicit RIBoostrap() {

  }
  struct FrameContext {
    struct RIScratchAlloc_s uboScratchAlloc;
    struct RICmd_s cmd;
    struct RIDescriptor_s colorAttachment;
    struct RIDescriptor_s depthAttachment;

    std::vector<RIFree> freelist;
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
    	struct VmaAllocation_T* pogoAlloc[RI_MAX_SWAPCHAIN_IMAGES * 2];
    	struct VmaAllocation_T* depthAlloc[RI_MAX_SWAPCHAIN_IMAGES];
		} vk;
#endif
	};
  RIRenderer_s renderer;
  RIDevice_s device;
  RISwapchain_s swapchain;
	RIProgram gui;

  RI_Format_e depthFormat;
	struct RIDescriptor_s colorAttachment[RI_MAX_SWAPCHAIN_IMAGES];
	struct RITexture_s depthTextures[RI_MAX_SWAPCHAIN_IMAGES];
	struct RIDescriptor_s depthAttachment[RI_MAX_SWAPCHAIN_IMAGES];

  struct RISegmentAlloc<RI_NUMBER_FRAMES_FLIGHT> guiVertexAlloc;
  RIBuffer_s guiVertexBuffer; 
  struct RISegmentAlloc<RI_NUMBER_FRAMES_FLIGHT> guiIndexAlloc;
  RIBuffer_s guiIndexBuffer;

  std::array<FrameContext, RI_NUMBER_FRAMES_FLIGHT> frameSets;
	std::array<RIDescriptor_s, 2048> cachedFilters; 
  uint32_t swapchainIndex;
  uint64_t frame_count = 0;

  struct RIResourceUploader_s uploader = {};

  void IncrementFrame();
  RIDescriptor_s *resolve_filter_descriptor(eTextureWrap wrapS, eTextureWrap wrapT, eTextureWrap wrapR, eTextureFilter filter);
  FrameContext *GetActiveSet() { return &frameSets[frame_count % RI_NUMBER_FRAMES_FLIGHT]; }

  void UpdateFrameUBO(RIDescriptor_s* descriptor, void* data, size_t size);

};
extern struct RIBoostrap RI; 

}; // namespace hpl

#endif
