#ifndef HPL_RENDERER_HYBRID_H
#define HPL_RENDERER_HYBRID_H

#include "graphics/BindlessPool.h"
#include "graphics/HPLGraphicsConfig.h"
#include "graphics/Material.h"
#include "graphics/RenderList2.h"
#include "graphics/Renderer.h"
#include "graphics/RITypes.h"
#include "graphics/RIRenderer.h"
#include "system/Hasher.h"

#include <array>
#include <vector>

namespace hpl {

struct SurfelElement {
  float m_pos_x,m_pos_y,m_pos_z, m_radius;
  float m_normal_x,m_normal_y,m_normal_z, m_pad_0;
};

// Mirrors `UniformObject` in amnesia/glsl/forward_shader_common.glsl (std430).
struct alignas(16) ObjectGPUData {
  float    dissolveAmount;
  uint32_t materialID;
  float    lightLevel;
  float    illuminationAmount;
  float    modelMat[16];
  float    invModelMat[16];
  float    uvMat[16];
};
static_assert(sizeof(ObjectGPUData) == 208,
              "must match forward_shader_common.glsl::UniformObject std430");

// Mirrors `PerFrameConstants` in amnesia/glsl/forward_shader_common.glsl (std140).
struct alignas(16) PerFrameConstants {
  float invViewRotationMat[16];
  float viewMat[16];
  float invViewMat[16];
  float projMat[16];
  float viewProjMat[16];
  float worldFogStart;
  float worldFogLength;
  float oneMinusFogAlpha;
  float fogFalloffExp;
  float worldFogColor[4];
  float viewTexel[2];
  float viewportSize[2];
  float afT;
  float _pad[3];
};

class cHybridRenderer : public iRenderer {
public:
  cHybridRenderer(cGraphics *apGraphics, cResources *apResources);
  ~cHybridRenderer();

  static constexpr uint32_t UBO_BUFFER_SIZE = 8 * (1024 * 1024); // 8 MB
  static constexpr uint32_t OBJECT_SLOT_CAPACITY = 4096;

  virtual void Draw(RIBootstrap::FrameContext *cntx, cViewport *viewport,
                    float afFrameTime, cFrustum *apFrustum, cWorld *apWorld,
                    cRenderSettings *apSettings,
                    bool abSendFrameBufferToPostEffects) override;

  virtual bool LoadData() override { return true; };
  virtual void DestroyData() override {};
  virtual void CopyToFrameBuffer() override {};
  virtual void SetupRenderList() override {};
  virtual void RenderObjects() override {};

private:

  // Per-(material, frame) version tracker. The renderer re-uploads a material's
  // packed block whenever the material pointer or its Generation() drifts from
  // what we wrote last frame. Triple-buffered so in-flight frames don't overwrite
  // each other's view of the data.
  //struct MaterialDescInfo {
  //  void *m_material = nullptr;
  //  uint32_t m_version = 0;
  //};
  //struct MaterialInfo {
  //  std::array<MaterialDescInfo, RI_NUMBER_FRAMES_FLIGHT> m_perFrame{};
  //};
  //std::array<MaterialInfo, cMaterial::MaxMaterialID> m_materialInfo{};
  
  cRenderList2 m_rendererList;
  BindlessPool m_objectSlot;
  std::vector<hash_t> m_objectPayloadHash;

  struct RIBuffer_s m_objectBuffer;
  struct RIBuffer_s m_indirectDrawBuffer;
  struct RISegmentAlloc<RI_NUMBER_FRAME_SEGMENTS> m_indirectSegment;

	RIProgram m_forwardDiffuse;

  // Triangle visibility buffer (location 1 of forward_diffuse.frag).
  // Sized to the swapchain; lazily (re)created when the cached extent drifts.
  struct RITexture_s m_visibilityTarget;
  struct RITextureView_s m_visibilityView;
  uint32_t m_visibilityWidth = 0;
  uint32_t m_visibilityHeight = 0;


  //struct RIBuffer_s positionBuffer;
  //struct RIBuffer_s normalBuffer;
  //struct RIBuffer_s colorBuffer;
  //struct RIBuffer_s uvBuffer;
};

} // namespace hpl

#endif
