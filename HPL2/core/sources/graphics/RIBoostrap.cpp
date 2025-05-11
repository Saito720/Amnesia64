#include "graphics/RIBoostrap.h"
#include "graphics/RIRenderer.h"
#include "graphics/RIVK.h"

namespace hpl {

	RIDescriptor_s* RIBoostrap::resolve_filter_descriptor(eTextureWrap wrapS, eTextureWrap wrapT, eTextureWrap wrapR, eTextureFilter filter) {
#if ( DEVICE_IMPL_VULKAN )
		{
			VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			info.addressModeU = RI_VK_TextureWrap(wrapS);
			info.addressModeV = RI_VK_TextureWrap(wrapR);
			info.addressModeW = RI_VK_TextureWrap(wrapT);
			switch(filter) {
      	case eTextureFilter_Nearest:
      		info.minFilter = VK_FILTER_NEAREST;
      		info.magFilter  = VK_FILTER_NEAREST;
      		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      		break;
      	case eTextureFilter_Bilinear:
      		info.minFilter = VK_FILTER_LINEAR;
      		info.magFilter  = VK_FILTER_LINEAR;
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
			const hash_t hash = hash_data( HASH_INITIAL_VALUE, &info, sizeof( VkSamplerCreateInfo ) );
			const size_t startIndex = ( hash % cached_filters.size());
			size_t index = startIndex;
			do {
				if( cached_filters[index].cookie == hash ) {
					return &cached_filters[index];
				} else if( RI_IsEmptyDescriptor(&cached_filters[index] ) ) {
					cached_filters[index].cookie = hash;
					cached_filters[index].vk.type = VK_DESCRIPTOR_TYPE_SAMPLER;
					cached_filters[index].vk.image.imageView = VK_NULL_HANDLE;
					cached_filters[index].vk.image.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					cached_filters[index].flags = RI_VK_DESC_OWN_SAMPLER;
					VK_WrapResult( vkCreateSampler( device.vk.device, &info, NULL, &cached_filters[index].vk.image.sampler ) );
					UpdateRIDescriptor( &device, &cached_filters[index] );
					return &cached_filters[index];
				}
				index = ( index + 1 ) % cached_filters.size();
			} while( index != startIndex );
		}
#endif
		return NULL;
	}


}
