#include "graphics/GraphicsTypes.h"
#include "graphics/RIBoostrap.h"
#include "graphics/HPLTexture.h"
#include "graphics/RIFormat.h"
#include "graphics/RIRenderer.h"
#include "graphics/RIVK.h"

#include "graphics/Bitmap.h"

#include <cassert>

namespace hpl {
void HPLTexture::HPLTexture_Delete(HPLTexture *texture) {
  vmaFreeMemory(RI.device.vk.vmaAllocator,
                texture->vk.vmaAlloc);
  vkDestroyImage(RI.device.vk.device, texture->handle.vk.image,
                 NULL);
  vkDestroyImageView(RI.device.vk.device,
                     texture->binding.vk.image.imageView, NULL);
  delete texture;
}

RI_Format to_image_supported_format(ePixelFormat format) {
  switch (format) {
  case ePixelFormat_Alpha:
  case ePixelFormat_Luminance:
    return RI_FORMAT_R8_UNORM;
  case ePixelFormat_LuminanceAlpha:
    return RI_FORMAT_RG8_UNORM;
  case ePixelFormat_RGB: // generally not supported most hardware does not
                         // support 24 bit formats
  case ePixelFormat_RGBA:
    return RI_FORMAT_RGBA8_UNORM;
  case ePixelFormat_BGRA:
    return RI_FORMAT_BGRA8_UNORM;
  case ePixelFormat_DXT1:
    return RI_FORMAT_BC1_RGBA_UNORM;
  case ePixelFormat_DXT2:
  case ePixelFormat_DXT3:
    return RI_FORMAT_BC2_RGBA_UNORM;
  case ePixelFormat_DXT4:
  case ePixelFormat_DXT5:
    return RI_FORMAT_BC3_RGBA_UNORM;
  case ePixelFormat_Depth16:
    return RI_FORMAT_D16_UNORM;
  case ePixelFormat_Depth24:
    return RI_FORMAT_D32_SFLOAT_S8_UINT;
  case ePixelFormat_Depth32:
    return RI_FORMAT_D32_SFLOAT;
  case ePixelFormat_Alpha16:
  case ePixelFormat_Luminance16:
    return RI_FORMAT_R16_UNORM;
  case ePixelFormat_LuminanceAlpha16:
    return RI_FORMAT_RG16_UNORM;
  case ePixelFormat_RGBA16:
  case ePixelFormat_RGB16:
    return RI_FORMAT_RGBA16_UNORM;
  case ePixelFormat_Alpha32:
  case ePixelFormat_Luminance32:
    return RI_FORMAT_R32_SFLOAT;
  case ePixelFormat_LuminanceAlpha32:
    return RI_FORMAT_RG32_SFLOAT;
  case ePixelFormat_RGBA32:
    return RI_FORMAT_RGBA32_SFLOAT;
  case ePixelFormat_BGR:
    return RI_FORMAT_BGRA8_UNORM;
  default:
    assert(false && "Unsupported texture format");
    break;
  }
  return RI_FORMAT_UNKNOWN;
}

bool HPLTexture::LoadBitmap(
                          RIBarrierImageHandle_s postBarrier,
                            cBitmap &bitmap, 
                            const BitmapLoadOptions &options) {
  assert(this->bootstrap);
  width = bitmap.GetWidth();
  height = bitmap.GetHeight();
  depth = bitmap.GetDepth();
  RI_Format destFormat = hpl::to_image_supported_format(bitmap.GetPixelFormat());
  VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  const struct RIFormatProps_s *formatProps = GetRIFormatProps(destFormat);
  if (formatProps->blockWidth > 1) {
    info.flags |=
        VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT; // format can be used
                                                         // to create a view
                                                         // with an uncompressed
                                                         // format (1 texel
                                                         // covers 1 block)
  }
  info.mipLevels = options.use_mipmaps ? bitmap.GetNumOfMipMaps() : 1;
  info.arrayLayers = options.use_array ? bitmap.GetNumOfImages() : 1;
  if (options.use_cubemap) {
    info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; // allow cube maps
    if (options.use_array) {
      assert(bitmap.GetNumOfImages() % 6 == 0 &&
             "Cube map array must have a multiple of 6 images");
      info.arrayLayers = bitmap.GetNumOfImages();
    } else {
      assert(bitmap.GetNumOfImages() == 6 && "Cube map must have 6 images");
      info.arrayLayers = 6;
    }
  }
  info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
               VK_IMAGE_CREATE_EXTENDED_USAGE_BIT; // typeless
  info.imageType = depth > 1
                       ? VK_IMAGE_TYPE_3D
                       : ((bitmap.GetWidth() == 1 || bitmap.GetHeight() == 1)
                              ? VK_IMAGE_TYPE_1D
                              : VK_IMAGE_TYPE_2D);
  info.format = RIFormatToVK(destFormat);
  info.extent.width = bitmap.GetWidth();
  info.extent.height = bitmap.GetHeight();
  info.extent.depth = bitmap.GetDepth();
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  uint32_t queueFamilies[RI_QUEUE_LEN] = {0};
  info.pQueueFamilyIndices = queueFamilies;
  VK_ConfigureImageQueueFamilies(&info, RI.device.queues, RI_QUEUE_LEN,
                                 queueFamilies, RI_QUEUE_LEN);
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VmaAllocationCreateInfo mem_reqs = {0};
  mem_reqs.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	
	VkImageViewUsageCreateInfo usageInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO };
	VkImageSubresourceRange subresource = {
		VK_IMAGE_ASPECT_COLOR_BIT, 0, std::max<uint32_t>(info.mipLevels, 1), 0, 1,
	};
	VkImageViewCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	if(!VK_WrapResult(vmaCreateImage(RI.device.vk.vmaAllocator, &info, &mem_reqs, &handle.vk.image, &vk.vmaAlloc, NULL))) {
	  return false;
	}

	usageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	createInfo.pNext = &usageInfo;
	createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	createInfo.format = RIFormatToVK( destFormat );
	createInfo.subresourceRange = subresource;
	createInfo.image = handle.vk.image;
		
	binding.flags |= RI_VK_DESC_OWN_IMAGE_VIEW;
	binding.texture = &handle;
	binding.vk.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	binding.vk.image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VK_WrapResult( vkCreateImageView( RI.device.vk.device, &createInfo, NULL, &binding.vk.image.imageView ) );
	RefreshCookies( &RI.device, &binding );

  auto sourceFormat = to_image_supported_format(bitmap.GetPixelFormat());
  const struct RIFormatProps_s* srcProps = GetRIFormatProps(sourceFormat);
  const struct RIFormatProps_s* destProps = GetRIFormatProps(destFormat);
#define MIP_REDUCE(s, mip) (std::max<uint32_t>(1u, (uint32_t)((s) >> (mip))))
  for (uint32_t arrIndex = 0; arrIndex < info.arrayLayers; arrIndex++) {
    for (uint32_t mipLevel = 0; mipLevel < info.mipLevels; mipLevel++) {
      struct RIResourceTextureTransaction_s uploadDesc = {0};
      
      const auto& input = bitmap.GetData(arrIndex, mipLevel);
      uploadDesc.target = handle;
      uploadDesc.width = MIP_REDUCE(info.extent.width, mipLevel);
      uploadDesc.height = MIP_REDUCE(info.extent.height, mipLevel);
      uploadDesc.sliceNum = uploadDesc.height;
      uploadDesc.rowPitch = uploadDesc.height * srcProps->stride;
      uploadDesc.arrayOffset = arrIndex;
      uploadDesc.mipOffset = mipLevel;
      uploadDesc.x = 0;
      uploadDesc.y = 0;
      uploadDesc.depth = info.extent.depth;
      uploadDesc.format = destFormat;
      uploadDesc.postBarrier = postBarrier;
      // #if ( DEVICE_IMPL_VULKAN )
      //	{
      //		uploadDesc.postBarrier.vk.layout =
      //VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      //		uploadDesc.postBarrier.vk.stage =
      //VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
      //		uploadDesc.postBarrier.vk.access =
      //VK_ACCESS_2_SHADER_READ_BIT;
      //	}
      // #endif
      RI_ResourceBeginCopyTexture(&RI.device, &RI.uploader, &uploadDesc);
      for (size_t z = 0; z < info.extent.depth; ++z) {
        for (size_t slice = 0; slice < uploadDesc.height; slice++) {
          const size_t dstRowStart = uploadDesc.alignRowPitch * slice;
          for (size_t column = 0; column < uploadDesc.width; column++) {
            if(destProps->isCompressed) {
					    memcpy( &( (uint8_t *)uploadDesc.data )[dstRowStart + ( destProps->stride * column )], 
					            &input->mpData[( uploadDesc.width * srcProps->stride * slice ) + ( column * srcProps->stride )], 
					          destProps->stride);
            } else {
              memset(&( (uint8_t *)uploadDesc.data )[dstRowStart + ( destProps->stride * column )], 0xff, destProps->stride);
					    memcpy( &( (uint8_t *)uploadDesc.data )[dstRowStart + ( destProps->stride * column )], 
					            &input->mpData[( uploadDesc.width * srcProps->stride * slice ) + ( column * srcProps->stride )], 
					          std::min<uint32_t>( srcProps->stride, destProps->stride ) );

            }
          }
        }
      }
      RI_ResourceEndCopyTexture(&RI.device, &RI.uploader, &uploadDesc);
    }
  }

        //if(vkSetDebugUtilsObjectNameEXT){
	//	VkDebugUtilsObjectNameInfoEXT debugName = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, NULL, VK_OBJECT_TYPE_IMAGE, (uint64_t)handle.vk.image, name.buf };
	//	VK_WrapResult( vkSetDebugUtilsObjectNameEXT( bootstrap->device.vk.device, &debugName ) );
	//}


  return true;
}

//HPLTexture::HPLTexture() {
//
//}
//
//HPLTexture::HPLTexture(struct RIResourceUploader_s* upload,cBitmap& bitmap, const BitmapLoadOptions& options) {
//	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
//	info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT; // typeless
//	const struct RIFormatProps_s *formatProps = GetRIFormatProps( destFormat );
//	//bitmap.GetPixelFormat()
//	////if( formatProps->blockWidth > 1 )
//	////	info.flags |= VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT; // format can be used to create a view with an uncompressed format (1 texel covers 1 block)
//	////if( image->flags & IT_CUBEMAP )
//	////	info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; // allow cube maps
//	//info.imageType = VK_IMAGE_TYPE_2D;
//	//info.format = RIFormatToVK( destFormat );
//	//info.extent.width = bitmap.GetWidth();
//	//info.extent.height = bitmap.GetHeight();
//	//info.extent.depth = 1;
//	//info.mipLevels = image->mipNum;
//	//info.arrayLayers = ( image->flags & IT_CUBEMAP ) ? 6 : 1;
//	//info.samples = 1;
//	//info.tiling = VK_IMAGE_TILING_OPTIMAL;
//	//info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
//
//	//info.pQueueFamilyIndices = queueFamilies;
//	//VK_ConfigureImageQueueFamilies( &info, rsh.device.queues, RI_QUEUE_LEN, queueFamilies, RI_QUEUE_LEN );
//	//info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
//	//
//	//VmaAllocationCreateInfo memCreateInfo = { 0 };
//	//memCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
//	//if(!VK_WrapResult(vmaCreateImage(rsh.device.vk.vmaAllocator, &info, &memCreateInfo, &image->handle.vk.image, &image->vk.vmaAlloc, NULL))) {
//	//	ri.Com_Printf( S_COLOR_YELLOW "Failed to Create Image: %s\n", image->name.buf );
//	//	__FreeImage( image );
//	//	image = NULL;
//	//	return NULL;
//	//}
//
//} 
//

} // namespace hpl
