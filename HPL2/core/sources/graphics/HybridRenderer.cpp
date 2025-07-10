#include "graphics/HybridRenderer.h"
#include "graphics/RITypes.h"

#include "graphics/Renderable.h"
#include "math/Frustum.h"
#include "math/Math.h"

#include "scene/RenderableContainer.h"

#include <functional>

namespace hpl {
cHybridRenderer::cHybridRenderer(cGraphics *apGraphics,
                                 cResources *apResources) 
		: iRenderer("Deferred",apGraphics, apResources, 0)

{
		uint32_t queueFamilies[RI_QUEUE_LEN] = { 0 };
		VkBufferCreateInfo vertexBufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		VK_ConfigureBufferQueueFamilies( &vertexBufferCreateInfo, RI.device.queues, RI_QUEUE_LEN, queueFamilies, RI_QUEUE_LEN );
		vertexBufferCreateInfo.pNext = NULL;
		vertexBufferCreateInfo.flags = 0;
		vertexBufferCreateInfo.size = UBO_BUFFER_SIZE;
		vertexBufferCreateInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		VmaAllocationInfo allocationInfo = { 0 };
		VmaAllocationCreateInfo allocInfo = { 0 };
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

		VK_WrapResult( vmaCreateBuffer( RI.device.vk.vmaAllocator, &vertexBufferCreateInfo, &allocInfo, &sceneUBO.vk.buffer, &sceneUBO.vk.alloc, &allocationInfo ) );
		if( vkSetDebugUtilsObjectNameEXT ) {
			VkDebugUtilsObjectNameInfoEXT debugName = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, NULL, VK_OBJECT_TYPE_BUFFER, (uint64_t)sceneUBO.vk.buffer, "VBO_VERTEX_BUFFER" };
			VK_WrapResult( vkSetDebugUtilsObjectNameEXT( RI.device.vk.device, &debugName ) );
		}

    VmaVirtualBlockCreateInfo allocCreateInfo = {};
    allocCreateInfo.size = UBO_BUFFER_SIZE;
    vmaCreateVirtualBlock(&allocCreateInfo, &sceneUBOVirtualAlloc);
}

static bool IsObjectIsVisible(iRenderable *object, tRenderableFlag neededFlags,
                              std::span<cPlanef> clipPlanes) {

  if (object->IsVisible() == false)
    return false;

  /////////////////////////////
  // Check flags
  if ((object->GetRenderFlags() & neededFlags) != neededFlags)
    return false;

  if (!clipPlanes.empty()) {
    cBoundingVolume *pBV = object->GetBoundingVolume();
    for (auto &planes : clipPlanes) {
      if (cMath::CheckPlaneBVCollision(planes, *pBV) == eCollision_Outside) {
        return false;
      }
    }
  }
  return true;
}

static void WalkAndPrepareRenderList(iRenderableContainer* container,cFrustum* frustum, std::function<void(iRenderable*)> handler, tRenderableFlag renderableFlag) {

  std::function<void(iRenderableContainerNode * childNode)> walkRenderables;
  walkRenderables = [&](iRenderableContainerNode* childNode) {
      childNode->UpdateBeforeUse();
      for (auto& childNode : childNode->GetChildNodes()) {
          childNode->UpdateBeforeUse();
          eCollision frustumCollision = frustum->CollideNode(childNode);
          if (frustumCollision == eCollision_Outside) {
              continue;
          }
          if (frustum->CheckAABBNearPlaneIntersection(childNode->GetMin(), childNode->GetMax())) {
              cVector3f vViewSpacePos = cMath::MatrixMul(frustum->GetViewMatrix(), childNode->GetCenter());
              childNode->SetViewDistance(vViewSpacePos.z);
              childNode->SetInsideView(true);
          } else {
              // Frustum origin is outside of node. Do intersection test.
              cVector3f vIntersection;
              cMath::CheckAABBLineIntersection(
                  childNode->GetMin(), childNode->GetMax(), frustum->GetOrigin(), childNode->GetCenter(), &vIntersection, NULL);
              cVector3f vViewSpacePos = cMath::MatrixMul(frustum->GetViewMatrix(), vIntersection);
              childNode->SetViewDistance(vViewSpacePos.z);
              childNode->SetInsideView(false);
          }
          walkRenderables(childNode);
      }
      for (auto& pObject : childNode->GetObjects()) {
          if (!IsObjectIsVisible(pObject, renderableFlag, {})) {
              continue;
          }
          handler(pObject);
      }
  };
  auto rootNode = container->GetRoot();
  rootNode->UpdateBeforeUse();
  rootNode->SetInsideView(true);
  walkRenderables(rootNode);
}

void cHybridRenderer::Draw(
    		RIBootstrap::FrameContext* cntx,
        cViewport* viewport,
        float afFrameTime,
        cFrustum* apFrustum,
        cWorld* apWorld,
        cRenderSettings* apSettings,
        bool abSendFrameBufferToPostEffects) {

} 

cHybridRenderer::~cHybridRenderer() {}

} // namespace hpl
