

#include "system/Hasher.h"
#include <array>
#include <span>
#include <unordered_map>
#include <vector>

#include "RIDescriptorSetAllocator.h"
#include "RITypes.h"
#include "graphics/spirv_reflect.h"
#include "resources/FileSearcher.h"
#include "system/SystemTypes.h"

namespace hpl {

  class RIProgram {
  public:
    static constexpr size_t DESCRIPTOR_SET_MAX = 3;
    struct PipelineSlot {
      union {
#if (DEVICE_IMPL_VULKAN)
        struct {
          VkPipeline handle;
        } vk;
#endif
      };
    };
    struct DescriptorSetSlot {
      union {
#if (DEVICE_IMPL_VULKAN)
        struct {
          VkDescriptorSetLayout setLayout;
        } vk;
#endif
      };
      struct RIDescriptorSetAlloc alloc; // the set allocator
      uint16_t samplerMaxNum;
      uint16_t constantBufferMaxNum;
      uint16_t dynamicConstantBufferMaxNum;
      uint16_t textureMaxNum;
      uint16_t storageTextureMaxNum;
      uint16_t bufferMaxNum;
      uint16_t storageBufferMaxNum;
      uint16_t structuredBufferMaxNum;
      uint16_t storageStructuredBufferMaxNum;
      uint16_t accelerationStructureMaxNum;
    };
    struct ShaderBinary {
      std::vector<char> buf;  
    };

    struct BindingReflection {
      hash_t hash;
      uint16_t isArray : 1;
      uint16_t dimCount : 8;
      uint16_t set : 3;
      uint16_t baseRegisterIndex;
    };

    enum ProgramStages { 
	    PROGRAM_STAGE_VERTEX, 
	    PROGRAM_STAGE_FRAGMENT, 
	    PROGRAM_STAGES_MAX 
    };
    struct ModuleStage {
      uint8_t stage;
      std::span<char> data;
    };
    const struct BindingReflection* find_reflection(const struct DescriptorBindingID& handle);
    void add_pipeline(struct RIDevice_s *device, hash_t hash,
                    VkGraphicsPipelineCreateInfo pipelineCreateInfo);
    static RIProgram create(std::span<ModuleStage> init);
    static std::vector<char> load_shader_stage(cFileSearcher *searcher, const tString& asName);
    RIProgram(RIProgram&&);
    RIProgram& operator=(RIProgram&&);
    explicit RIProgram() {
    }

  private:
    RIProgram(const RIProgram&) = delete;
    union __impl {
      struct {
#if (DEVICE_IMPL_VULKAN)
        struct {
          VkShaderStageFlags shader_stage_flags;
          uint32_t size;
        } pushConstant;
        VkPipelineLayout pipeline_layout;
#endif
      } vk;
    } impl;
    uint16_t reflection_len = 0;
    uint16_t vertex_input_mask = 0;
    bool has_push_constants = 0;
    std::array<struct DescriptorSetSlot, DESCRIPTOR_SET_MAX> program_descriptors;
    std::array<ShaderBinary, PROGRAM_STAGES_MAX> shader_bin;
    std::unordered_map<hash_t, PipelineSlot> pipeline;
    std::vector<BindingReflection> binding_reflection;
  };
} // namespace hpl
