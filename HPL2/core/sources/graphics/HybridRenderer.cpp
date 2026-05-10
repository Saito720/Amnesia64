#include "graphics/HybridRenderer.h"
#include "graphics/RITypes.h"

#include "graphics/GraphicUtils.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/MaterialResource.h"
#include "graphics/RIBootstrap.h"
#include "graphics/RIResourceUploader.h"
#include "graphics/Renderable.h"
#include "graphics/RIVK.h"
#include "graphics/VertexBuffer.h"
#include "graphics/VertexBuffer_RI.h"
#include "math/Frustum.h"
#include "math/Math.h"

#include "resources/Resources.h"
#include "scene/RenderableContainer.h"
#include "scene/World.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

namespace hpl {

namespace detail {
// uint32_t resolveTextureFilterGroup(cMaterial::TextureAntistropy anisotropy,
//                                    eTextureWrap wrap, eTextureFilter filter)
//                                    {
//   const uint32_t anisotropyGroup =
//       (static_cast<uint32_t>(eTextureFilter_LastEnum) *
//        static_cast<uint32_t>(eTextureWrap_LastEnum)) *
//       static_cast<uint32_t>(anisotropy);
//   return anisotropyGroup + ((static_cast<uint32_t>(wrap) *
//                              static_cast<uint32_t>(eTextureFilter_LastEnum))
//                              +
//                             static_cast<uint32_t>(filter));
// }

// Bind the four vertex streams forward_diffuse.vert expects (location 0..3) +
// the renderable's index buffer. Returns false if the position stream or index
// buffer is missing — caller should skip the dispatch in that case.
static bool bindForwardGeometry(VkCommandBuffer cmd, iVertexBuffer *pVB) {
  auto *vb = static_cast<VertexBuffer_RI *>(pVB);
  const eVertexBufferElement wanted[4] = {
      eVertexBufferElement_Position, eVertexBufferElement_Texture0,
      eVertexBufferElement_Normal,   eVertexBufferElement_Texture1Tangent};

  VkBuffer buffers[4] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                          VK_NULL_HANDLE};
  VkDeviceSize offsets[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4; ++i) {
    const auto *element = vb->GetElement(wanted[i]);
    if (element && element->buffer) {
      buffers[i] = element->buffer->vk.buffer;
    }
  }
  vkCmdBindVertexBuffers(cmd, 0, 4, buffers, offsets);

  const auto &indexBuf = vb->GetIndexRIBuffer();
  if (!indexBuf || indexBuf->vk.buffer == VK_NULL_HANDLE)
    return false;
  vkCmdBindIndexBuffer(cmd, indexBuf->vk.buffer, 0, VK_INDEX_TYPE_UINT32);
  return buffers[0] != VK_NULL_HANDLE;
}
} // namespace detail

cHybridRenderer::cHybridRenderer(cGraphics *apGraphics, cResources *apResources)
    : iRenderer("Hybrid", apGraphics, apResources, 0),
      m_objectSlot(OBJECT_SLOT_CAPACITY, RI_NUMBER_FRAMES_FLIGHT),
      m_objectPayloadHash(OBJECT_SLOT_CAPACITY, 0)

{
  {
    auto vert_stage = RIProgram::loadShaderStage(apResources->GetFileSearcher(),
                                                 "forward_diffuse.vert.spv");
    auto frag_stage = RIProgram::loadShaderStage(apResources->GetFileSearcher(),
                                                 "forward_diffuse.frag.spv");
    std::array<RIProgram::ModuleStage, 2> stages = {
        RIProgram::ModuleStage{RIProgram::PROGRAM_STAGE_VERTEX, vert_stage},
        RIProgram::ModuleStage{RIProgram::PROGRAM_STAGE_FRAGMENT, frag_stage}};
    m_forwardDiffuse.initialize(&RI.device, stages);
  }
  // uint32_t queueFamilies[RI_QUEUE_LEN] = { 0 };
  // VkBufferCreateInfo vertexBufferCreateInfo = {
  // VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO }; VK_ConfigureBufferQueueFamilies(
  // &vertexBufferCreateInfo, RI.device.queues, RI_QUEUE_LEN, queueFamilies,
  // RI_QUEUE_LEN ); vertexBufferCreateInfo.pNext = NULL;
  // vertexBufferCreateInfo.flags = 0;
  // vertexBufferCreateInfo.size = UBO_BUFFER_SIZE;
  // vertexBufferCreateInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
  // VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  // VmaAllocationInfo allocationInfo = { 0 };
  // VmaAllocationCreateInfo allocInfo = { 0 };
  // allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

  // VK_WrapResult( vmaCreateBuffer( RI.device.vk.vmaAllocator,
  // &vertexBufferCreateInfo, &allocInfo, &sceneUBO.vk.buffer,
  // &sceneUBO.vk.alloc, &allocationInfo ) ); if( vkSetDebugUtilsObjectNameEXT )
  // { 	VkDebugUtilsObjectNameInfoEXT debugName = {
  // VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, NULL,
  // VK_OBJECT_TYPE_BUFFER, (uint64_t)sceneUBO.vk.buffer, "VBO_VERTEX_BUFFER" };
  //	VK_WrapResult( vkSetDebugUtilsObjectNameEXT( RI.device.vk.device,
  //&debugName ) );
  // }

  // VmaVirtualBlockCreateInfo allocCreateInfo = {};
  // allocCreateInfo.size = UBO_BUFFER_SIZE;
  // vmaCreateVirtualBlock(&allocCreateInfo, &sceneUBOVirtualAlloc);
}

void cHybridRenderer::Draw(RIBootstrap::FrameContext *cntx, cViewport *viewport,
                           float afFrameTime, cFrustum *apFrustum,
                           cWorld *apWorld, cRenderSettings *apSettings,
                           bool abSendFrameBufferToPostEffects) {
  ml::float4x4 mainFrustumViewInvMat = apFrustum->GetViewMat();
  mainFrustumViewInvMat.Invert();
  const ml::float4x4 mainFrustumViewMat = apFrustum->GetViewMat();
  const ml::float4x4 mainFrustumProjMat = apFrustum->GetProjectionMat();
  {
    m_rendererList.BeginAndReset(afFrameTime, apFrustum);
    auto *dynamicContainer =
        apWorld->GetRenderableContainer(eWorldContainerType_Dynamic);
    auto *staticContainer =
        apWorld->GetRenderableContainer(eWorldContainerType_Static);
    dynamicContainer->UpdateBeforeRendering();
    staticContainer->UpdateBeforeRendering();

    auto prepareObjectHandler = [&](iRenderable *pObject) {
      if (!rendering::IsObjectIsVisible(
              pObject, eRenderableFlag_VisibleInNonReflection, {})) {
        return;
      }
      m_rendererList.AddObject(pObject);
    };
    rendering::WalkAndPrepareRenderList(dynamicContainer, apFrustum,
                                        prepareObjectHandler,
                                        eRenderableFlag_VisibleInNonReflection);
    rendering::WalkAndPrepareRenderList(staticContainer, apFrustum,
                                        prepareObjectHandler,
                                        eRenderableFlag_VisibleInNonReflection);
    m_rendererList.End(
        eRenderListCompileFlag_Diffuse | eRenderListCompileFlag_Translucent |
        eRenderListCompileFlag_Decal | eRenderListCompileFlag_Illumination |
        eRenderListCompileFlag_FogArea);
  }
  auto solids = m_rendererList.GetSolidObjects();

  // Triangle-visibility render target — sized to the swapchain. Recreated
  // when the cached extent drifts. R32_UINT to hold packed (drawId|primId).
  if (m_visibilityTarget.vk.image == VK_NULL_HANDLE ||
      m_visibilityWidth != RI.swapchain.width ||
      m_visibilityHeight != RI.swapchain.height) {
    if (m_visibilityView.vk.image) {
      cntx->freelist.push_back(RIFree(m_visibilityView.vk.image));
    }
    if (m_visibilityTarget.vk.image) {
      cntx->freelist.push_back(RIFree(m_visibilityTarget.vk.image));
      cntx->freelist.push_back(RIFree(m_visibilityTarget.vk.allocation));
    }

    VkImageCreateInfo imageCreateInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R32_UINT;
    imageCreateInfo.extent = {RI.swapchain.width, RI.swapchain.height, 1};
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo = {0};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VK_WrapResult(vmaCreateImage(RI.device.vk.vmaAllocator, &imageCreateInfo,
                                 &allocInfo, &m_visibilityTarget.vk.image,
                                 &m_visibilityTarget.vk.allocation, NULL));

    VkImageViewCreateInfo viewCreateInfo = {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewCreateInfo.image = m_visibilityTarget.vk.image;
    viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCreateInfo.format = VK_FORMAT_R32_UINT;
    viewCreateInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_WrapResult(vkCreateImageView(RI.device.vk.device, &viewCreateInfo, NULL,
                                    &m_visibilityView.vk.image));

    m_visibilityWidth = RI.swapchain.width;
    m_visibilityHeight = RI.swapchain.height;
  }

  // PerFrame UBO — std140 mirror of forward_shader_common.glsl PerFrameConstants.
  PerFrameConstants perFrame{};
  std::memcpy(perFrame.viewMat, mainFrustumViewMat.a, sizeof(perFrame.viewMat));
  std::memcpy(perFrame.invViewMat, mainFrustumViewInvMat.a,
              sizeof(perFrame.invViewMat));
  std::memcpy(perFrame.projMat, mainFrustumProjMat.a, sizeof(perFrame.projMat));
  // viewProjMat = proj * view (column-major); fill via direct ml composition
  // when needed. Leaving as identity-stub for now — first pass writes only
  // visibility; lighting in the FS reads viewMat/invViewMat which are correct.
  perFrame.viewportSize[0] = (float)RI.swapchain.width;
  perFrame.viewportSize[1] = (float)RI.swapchain.height;
  perFrame.viewTexel[0] =
      RI.swapchain.width ? 1.0f / (float)RI.swapchain.width : 0.0f;
  perFrame.viewTexel[1] =
      RI.swapchain.height ? 1.0f / (float)RI.swapchain.height : 0.0f;
  perFrame.afT = afFrameTime;
  // Fog params + worldFogColor + invViewRotationMat default to zero — fine for
  // the first pass; populate when the deferred-fog path needs them.

  // (a) Object buffer: fixed-capacity SSBO indexed by BindlessPool slot id.
  if (!IsRIBufferValid(&RI.renderer, &m_objectBuffer)) {
    uint32_t queueFamilies[RI_QUEUE_LEN] = {0};
    VkBufferCreateInfo bufferCreateInfo = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VK_ConfigureBufferQueueFamilies(&bufferCreateInfo, RI.device.queues,
                                    RI_QUEUE_LEN, queueFamilies, RI_QUEUE_LEN);
    bufferCreateInfo.size = OBJECT_SLOT_CAPACITY * sizeof(ObjectGPUData);
    bufferCreateInfo.usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationInfo allocationInfo = {0};
    VmaAllocationCreateInfo allocInfo = {0};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VK_WrapResult(vmaCreateBuffer(RI.device.vk.vmaAllocator, &bufferCreateInfo,
                                  &allocInfo, &m_objectBuffer.vk.buffer,
                                  &m_objectBuffer.vk.allocation,
                                  &allocationInfo));
    m_objectBuffer.mappedAddress = allocationInfo.pMappedData;
  }

  // (b) Indirect-draw buffer: ring-allocated through m_indirectSegment.
  RISegmentReq_s indirectReq = {};
  if (!IsRIBufferValid(&RI.renderer, &m_indirectDrawBuffer) ||
      !m_indirectSegment.request(RI.frameIndex, solids.size(), &indirectReq)) {
    struct RISegmentAllocDesc_s segmentAllocDesc = {0};
    segmentAllocDesc.numSegments = RI_NUMBER_FRAMES_FLIGHT;
    segmentAllocDesc.elementStride = sizeof(VkDrawIndexedIndirectCommand);
    segmentAllocDesc.maxElements =
        std::max<size_t>(m_indirectSegment.maxElements, 1024);
    while (segmentAllocDesc.maxElements < solids.size()) {
      segmentAllocDesc.maxElements += (segmentAllocDesc.maxElements >> 1);
    }
    m_indirectSegment =
        RISegmentAlloc<RI_NUMBER_FRAME_SEGMENTS>(&segmentAllocDesc);
    bool ok =
        m_indirectSegment.request(RI.frameIndex, solids.size(), &indirectReq);
    assert(ok);
    (void)ok;

    uint32_t queueFamilies[RI_QUEUE_LEN] = {0};
    VkBufferCreateInfo indirectBufferCreateInfo = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VK_ConfigureBufferQueueFamilies(&indirectBufferCreateInfo, RI.device.queues,
                                    RI_QUEUE_LEN, queueFamilies, RI_QUEUE_LEN);
    indirectBufferCreateInfo.size =
        segmentAllocDesc.maxElements * segmentAllocDesc.elementStride;
    indirectBufferCreateInfo.usage =
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationInfo allocationInfo = {0};
    VmaAllocationCreateInfo allocInfo = {0};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    if (m_indirectDrawBuffer.vk.buffer) {
      cntx->freelist.push_back(RIFree(m_indirectDrawBuffer.vk.buffer));
      cntx->freelist.push_back(RIFree(m_indirectDrawBuffer.vk.allocation));
    }
    VK_WrapResult(
        vmaCreateBuffer(RI.device.vk.vmaAllocator, &indirectBufferCreateInfo,
                        &allocInfo, &m_indirectDrawBuffer.vk.buffer,
                        &m_indirectDrawBuffer.vk.allocation, &allocationInfo));
    m_indirectDrawBuffer.mappedAddress = allocationInfo.pMappedData;
  }

  // (c) Walk solids — fill object buffer (dirty-tracked) and indirect buffer,
  // group contiguous (vb, material) runs into batches. cRenderList2 already
  // sorts by material so identical runs land adjacent.
  auto *indirectDst = reinterpret_cast<VkDrawIndexedIndirectCommand *>(
      static_cast<uint8_t *>(m_indirectDrawBuffer.mappedAddress) +
      indirectReq.elementOffset * indirectReq.elementStride);

  struct DrawBatch {
    iVertexBuffer *vb;
    cMaterial *material;
    uint32_t firstDraw;
    uint32_t drawCount;
  };
  std::vector<DrawBatch> batches;
  batches.reserve(64);

  iVertexBuffer *prevVB = nullptr;
  cMaterial *prevMat = nullptr;
  uint32_t writtenDraws = 0;

  for (iRenderable *pObject : solids) {
    cMatrixf *pMtx = pObject->GetModelMatrix(apFrustum);
    iVertexBuffer *pVB = pObject->GetVertexBuffer();
    cMaterial *pMat = pObject->GetMaterial();
    if (!pMtx || !pVB || !pMat)
      continue;

    cMatrixf *pInv = pObject->GetInvModelMatrix();

    // Row-major copy; GPU column-major remap deferred to Phase C.
    ObjectGPUData payload{};
    payload.dissolveAmount = pObject->GetCoverageAmount();
    payload.materialID = 0; // populated by Phase C material upload
    payload.lightLevel = 1.0f;
    payload.illuminationAmount = pObject->GetIlluminationAmount();
    std::memcpy(payload.modelMat, pMtx->v, sizeof(payload.modelMat));
    if (pInv)
      std::memcpy(payload.invModelMat, pInv->v, sizeof(payload.invModelMat));
    std::memcpy(payload.uvMat, cMatrixf::Identity.v, sizeof(payload.uvMat));

    const hash_t cookie =
        hash_u64(HASH_INITIAL_VALUE, (uint64_t)(uintptr_t)pObject);
    const hash_t payloadHash =
        hash_data(HASH_INITIAL_VALUE, &payload, sizeof(payload));
    auto req = m_objectSlot.request(cookie, (uint32_t)RI.frameIndex);

    if (!req.found || m_objectPayloadHash[req.id] != payloadHash) {
      std::memcpy(static_cast<uint8_t *>(m_objectBuffer.mappedAddress) +
                      req.id * sizeof(ObjectGPUData),
                  &payload, sizeof(payload));
      m_objectPayloadHash[req.id] = payloadHash;
    }

    // firstInstance carries the slot id to the VS via gl_InstanceIndex.
    indirectDst[writtenDraws] = VkDrawIndexedIndirectCommand{
        .indexCount = (uint32_t)pVB->GetIndexNum(),
        .instanceCount = 1,
        .firstIndex = 0,
        .vertexOffset = 0,
        .firstInstance = req.id,
    };

    if (pVB == prevVB && pMat == prevMat && !batches.empty()) {
      batches.back().drawCount++;
    } else {
      batches.push_back({pVB, pMat, writtenDraws, 1});
      prevVB = pVB;
      prevMat = pMat;
    }
    ++writtenDraws;
  }

  if (writtenDraws == 0)
    return;

  // ---------- First pass: forward MRT (color + visibility) ----------
  VkCommandBuffer cmd = RI.primary.cmds[0].vk.cmd;

  // Bindless design (set 0 textures_2d[]/textures_cube[]/textures_2d_array[])
  // is owned by RI.bindlessTextureSlot — bound separately when that registry
  // wires up. forward_resource.glsl now puts:
  //   set 1: perFrame UBO + lightClustersCount/Buf + pointLightsBuf
  //   set 2: sceneObjectsBuf + opaqueMaterialBuf
  // Bindings below resolve by reflection name → set/binding regardless of
  // numbering changes.
  RIProgram::DescriptorBinding bindings[5] = {0};
  size_t numBindings = 0;

  // sceneObjectsBuf (set 2, binding 0) — real, from Phase A
  RIDescriptor_s sceneObjectsDesc = {};
  sceneObjectsDesc.cookie =
      hash_u64(HASH_INITIAL_VALUE, (uint64_t)(uintptr_t)&m_objectBuffer);
  sceneObjectsDesc.vk.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  sceneObjectsDesc.vk.buffer.buffer = m_objectBuffer.vk.buffer;
  sceneObjectsDesc.vk.buffer.offset = 0;
  sceneObjectsDesc.vk.buffer.range =
      OBJECT_SLOT_CAPACITY * sizeof(ObjectGPUData);
  bindings[numBindings].descriptor = sceneObjectsDesc;
  bindings[numBindings++].handle = DescriptorBindingID::Create("sceneObjectsBuf");

  // perFrame (set 1, binding 0) — real, scratch UBO
  RI.UpdateFrameUBO(&bindings[numBindings].descriptor, &perFrame,
                    sizeof(perFrame));
  bindings[numBindings++].handle = DescriptorBindingID::Create("perFrame");

  // TODO(B.3) opaqueMaterialBuf      (set 2, binding 1)
  // TODO(B.5) lightClustersCountBuf  (set 1, binding 3)
  // TODO(B.5) lightClustersBuf       (set 1, binding 4)
  // TODO(B.5) pointLightsBuf         (set 1, binding 5)
  // TODO(B.4) bindless textures_2d[] (set 0, binding 0) — bound via RI registry

  // Close Scene's outer rendering so we can attach our second color target.
  vkCmdEndRendering(cmd);

  // Visibility target: UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL.
  {
    VkImageMemoryBarrier2 toColor = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toColor.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    toColor.srcAccessMask = 0;
    toColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.image = m_visibilityTarget.vk.image;
    toColor.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &toColor;
    vkCmdPipelineBarrier2(cmd, &dep);
  }

  // Begin our 2-color rendering: [swapchain color, R32_UINT visibility] + depth.
  VkRenderingAttachmentInfo colorAttachments[2] = {
      {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO},
      {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}};
  RI_VK_FillColorAttachment(&colorAttachments[0], &cntx->colorAttachment,
                            /*attachAndClear=*/false);
  colorAttachments[1].imageView = m_visibilityView.vk.image;
  colorAttachments[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachments[1].clearValue.color.uint32[0] = 0;

  VkRenderingAttachmentInfo depthAttachment = {
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  RI_VK_FillDepthAttachment(&depthAttachment, &RI.depthView[RI.swapchainIndex],
                            /*attachAndClear=*/false);

  VkRenderingInfo renderingInfo = {VK_STRUCTURE_TYPE_RENDERING_INFO};
  renderingInfo.renderArea = {{0, 0}, {RI.swapchain.width, RI.swapchain.height}};
  renderingInfo.layerCount = 1;
  renderingInfo.colorAttachmentCount = 2;
  renderingInfo.pColorAttachments = colorAttachments;
  renderingInfo.pDepthAttachment = &depthAttachment;
  vkCmdBeginRendering(cmd, &renderingInfo);

  VkViewport vkViewport = {0,
                           (float)RI.swapchain.height,
                           (float)RI.swapchain.width,
                           -(float)RI.swapchain.height,
                           0.0f,
                           1.0f};
  VkRect2D scissor = {{0, 0}, {RI.swapchain.width, RI.swapchain.height}};
  vkCmdSetViewport(cmd, 0, 1, &vkViewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  // Pipeline create info — stable for this pass; bindPipeline caches by hash.
  VkVertexInputBindingDescription vertexBindings[4] = {
      {0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX},
      {2, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX},
      {3, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX},
  };
  VkVertexInputAttributeDescription vertexAttribs[4] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
      {1, 1, VK_FORMAT_R32G32_SFLOAT, 0},
      {2, 2, VK_FORMAT_R32G32B32_SFLOAT, 0},
      {3, 3, VK_FORMAT_R32G32B32_SFLOAT, 0},
  };
  VkPipelineVertexInputStateCreateInfo vertexInputState = {
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertexInputState.vertexBindingDescriptionCount = 4;
  vertexInputState.pVertexBindingDescriptions = vertexBindings;
  vertexInputState.vertexAttributeDescriptionCount = 4;
  vertexInputState.pVertexAttributeDescriptions = vertexAttribs;

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineRasterizationStateCreateInfo rasterizationState = {
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizationState.lineWidth = 1.0f;

  VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                    VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState = {
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamicState.dynamicStateCount = ARRAY_COUNT(dynamicStates);
  dynamicState.pDynamicStates = dynamicStates;

  VkFormat colorFormats[2] = {RIFormatToVK(RI.swapchain.format),
                              VK_FORMAT_R32_UINT};
  VkPipelineRenderingCreateInfo pipelineRendering = {
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  pipelineRendering.colorAttachmentCount = 2;
  pipelineRendering.pColorAttachmentFormats = colorFormats;
  pipelineRendering.depthAttachmentFormat = RIFormatToVK(RI.depthFormat);

  VkPipelineViewportStateCreateInfo viewportState = {
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineMultisampleStateCreateInfo multisampleState = {
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depthStencilState = {
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depthStencilState.depthTestEnable = VK_TRUE;
  depthStencilState.depthWriteEnable = VK_TRUE;
  depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depthStencilState.minDepthBounds = 0.0f;
  depthStencilState.maxDepthBounds = 1.0f;

  VkPipelineColorBlendAttachmentState blendAttachments[2] = {
      {VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
       VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
       VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT},
      {VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
       VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
       VK_COLOR_COMPONENT_R_BIT},
  };
  VkPipelineColorBlendStateCreateInfo colorBlendState = {
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  colorBlendState.attachmentCount = 2;
  colorBlendState.pAttachments = blendAttachments;

  VkGraphicsPipelineCreateInfo pipelineCreateInfo = {
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipelineCreateInfo.pNext = &pipelineRendering;
  pipelineCreateInfo.pVertexInputState = &vertexInputState;
  pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
  pipelineCreateInfo.pRasterizationState = &rasterizationState;
  pipelineCreateInfo.pDynamicState = &dynamicState;
  pipelineCreateInfo.pViewportState = &viewportState;
  pipelineCreateInfo.pMultisampleState = &multisampleState;
  pipelineCreateInfo.pDepthStencilState = &depthStencilState;
  pipelineCreateInfo.pColorBlendState = &colorBlendState;

  hash_t pipelineHash = hash_u32(HASH_INITIAL_VALUE, RI.swapchain.format);
  pipelineHash = hash_u32(pipelineHash, RI.depthFormat);
  m_forwardDiffuse.bindPipeline(&RI.device, &RI.primary.cmds[0], pipelineHash,
                                "hybrid.forward_diffuse_mrt",
                                &pipelineCreateInfo);
  m_forwardDiffuse.bindDescriptors(&RI.device, &RI.primary.cmds[0],
                                   RI.frameIndex, bindings, numBindings);

  // Per-batch dispatch.
  const VkDeviceSize stride = sizeof(VkDrawIndexedIndirectCommand);
  for (const auto &batch : batches) {
    if (!detail::bindForwardGeometry(cmd, batch.vb))
      continue;
    const VkDeviceSize offset =
        (indirectReq.elementOffset + batch.firstDraw) * stride;
    vkCmdDrawIndexedIndirect(cmd, m_indirectDrawBuffer.vk.buffer, offset,
                             batch.drawCount, (uint32_t)stride);
  }

  vkCmdEndRendering(cmd);

  // Visibility target: COLOR_ATTACHMENT -> SHADER_READ_ONLY (consumer ready).
  {
    VkImageMemoryBarrier2 toRead = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toRead.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toRead.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toRead.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    toRead.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.image = m_visibilityTarget.vk.image;
    toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &toRead;
    vkCmdPipelineBarrier2(cmd, &dep);
  }

  // Re-begin Scene's *original* rendering — single color (swapchain) + depth —
  // so cScene::Render's GUI/3D-Gui calls see the rendering they expected.
  VkRenderingAttachmentInfo restoreColor = {
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  RI_VK_FillColorAttachment(&restoreColor, &cntx->colorAttachment,
                            /*attachAndClear=*/false);
  VkRenderingAttachmentInfo restoreDepth = {
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  RI_VK_FillDepthAttachment(&restoreDepth, &RI.depthView[RI.swapchainIndex],
                            /*attachAndClear=*/false);
  VkRenderingInfo restoreInfo = {VK_STRUCTURE_TYPE_RENDERING_INFO};
  restoreInfo.renderArea = {{0, 0},
                            {RI.swapchain.width, RI.swapchain.height}};
  restoreInfo.layerCount = 1;
  restoreInfo.colorAttachmentCount = 1;
  restoreInfo.pColorAttachments = &restoreColor;
  restoreInfo.pDepthAttachment = &restoreDepth;
  vkCmdBeginRendering(cmd, &restoreInfo);
}

cHybridRenderer::~cHybridRenderer() {}

} // namespace hpl
