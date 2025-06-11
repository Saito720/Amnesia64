#include "graphics/RIBoostrap.h"
#include "graphics/RIRenderer.h"
#include "graphics/RIVK.h"

namespace hpl {

RIBoostrap RI = RIBoostrap{};

void RIBoostrap::IncrementFrame() {
  // FrameContext* cntx = GetActiveSet();
  frameIndex++;
}

void RIBoostrap::UpdateFrameUBO(RIDescriptor_s *descriptor, void *data, size_t size) {
	auto* activeSet = GetActiveSet();
	const hash_t hash = hash_data_hsieh( HASH_INITIAL_VALUE + frameIndex, data, size );
	if( descriptor->cookie != hash ) {
		descriptor->cookie = hash;
		struct RIBufferScratchAllocReq_s scratchReq = RIAllocBufferFromScratchAlloc( &device, &activeSet->uboScratchAlloc, size );
		descriptor->vk.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptor->vk.buffer.buffer = scratchReq.block.vk.buffer;
		descriptor->vk.buffer.offset = scratchReq.bufferOffset;
		descriptor->vk.buffer.range = size;
		memcpy( (uint8_t*)scratchReq.pMappedAddress + scratchReq.bufferOffset, data, size );
		RIFinishScrachReq( &device, &scratchReq );
	}

}

RIDescriptor_s *RIBoostrap::resolve_filter_descriptor(eTextureWrap wrapS,
                                                      eTextureWrap wrapT,
                                                      eTextureWrap wrapR,
                                                      eTextureFilter filter) {
#if (DEVICE_IMPL_VULKAN)
  {
    VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.addressModeU = RI_VK_TextureWrap(wrapS);
    info.addressModeV = RI_VK_TextureWrap(wrapR);
    info.addressModeW = RI_VK_TextureWrap(wrapT);
    switch (filter) {
    case eTextureFilter_Nearest:
      info.minFilter = VK_FILTER_NEAREST;
      info.magFilter = VK_FILTER_NEAREST;
      info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case eTextureFilter_Bilinear:
      info.minFilter = VK_FILTER_LINEAR;
      info.magFilter = VK_FILTER_LINEAR;
      info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case eTextureFilter_Trilinear:
      info.minFilter = VK_FILTER_LINEAR;
      info.magFilter = VK_FILTER_LINEAR;
      info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    case eTextureFilter_LastEnum:
      assert(false);
      break;
    }
    const hash_t hash =
        hash_data(HASH_INITIAL_VALUE, &info, sizeof(VkSamplerCreateInfo));
    const size_t startIndex = (hash % cachedFilters.size());
    size_t index = startIndex;
    do {
      if (cachedFilters[index].cookie == hash) {
        return &cachedFilters[index];
      } else if (RI_IsEmptyDescriptor(&cachedFilters[index])) {
        cachedFilters[index].cookie = hash;
        cachedFilters[index].vk.type = VK_DESCRIPTOR_TYPE_SAMPLER;
        cachedFilters[index].vk.image.imageView = VK_NULL_HANDLE;
        cachedFilters[index].vk.image.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        cachedFilters[index].flags = RI_VK_DESC_OWN_SAMPLER;
        VK_WrapResult(vkCreateSampler(device.vk.device, &info, NULL,
                                      &cachedFilters[index].vk.image.sampler));
        RIFinalizeDescriptor(&device, &cachedFilters[index]);
        return &cachedFilters[index];
      }
      index = (index + 1) % cachedFilters.size();
    } while (index != startIndex);
  }
#endif
  return NULL;
}

} // namespace hpl
