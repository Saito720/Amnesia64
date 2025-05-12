#include "graphics/RIBoostrap.h"
#include "graphics/RIHandles.h"
#include "graphics/RIFormat.h"

namespace hpl {
void HPLTexture::HPLTexture_Delete::operator()(HPLTexture *texture) {
  vmaFreeMemory(texture->bootstrap->device.vk.vmaAllocator,
                texture->vk.vmaAlloc);
  vkDestroyImage(texture->bootstrap->device.vk.device, texture->handle.vk.image,
                 NULL);
  vkDestroyImageView(texture->bootstrap->device.vk.device,
                     texture->binding.vk.image.imageView, NULL);
}

HPLTexture::HPLTexture() {

} 
HPLTexture::HPLTexture(struct RIResourceUploader_s* upload,cBitmap& bitmap, const BitmapLoadOptions& options) {
	//VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	//info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT; // typeless
	//const struct RIFormatProps_s *formatProps = GetRIFormatProps( destFormat );
	//bitmap.GetPixelFormat()
	////if( formatProps->blockWidth > 1 )
	////	info.flags |= VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT; // format can be used to create a view with an uncompressed format (1 texel covers 1 block)
	////if( image->flags & IT_CUBEMAP )
	////	info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; // allow cube maps
	//info.imageType = VK_IMAGE_TYPE_2D;
	//info.format = RIFormatToVK( destFormat );
	//info.extent.width = bitmap.GetWidth();
	//info.extent.height = bitmap.GetHeight();
	//info.extent.depth = 1;
	//info.mipLevels = image->mipNum;
	//info.arrayLayers = ( image->flags & IT_CUBEMAP ) ? 6 : 1;
	//info.samples = 1;
	//info.tiling = VK_IMAGE_TILING_OPTIMAL;
	//info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	//info.pQueueFamilyIndices = queueFamilies;
	//VK_ConfigureImageQueueFamilies( &info, rsh.device.queues, RI_QUEUE_LEN, queueFamilies, RI_QUEUE_LEN );
	//info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	//
	//VmaAllocationCreateInfo memCreateInfo = { 0 };
	//memCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	//if(!VK_WrapResult(vmaCreateImage(rsh.device.vk.vmaAllocator, &info, &memCreateInfo, &image->handle.vk.image, &image->vk.vmaAlloc, NULL))) {
	//	ri.Com_Printf( S_COLOR_YELLOW "Failed to Create Image: %s\n", image->name.buf );
	//	__FreeImage( image );
	//	image = NULL;
	//	return NULL;
	//}

} 


} // namespace hpl
