#include "graphics/RIProgram.h"
#include "system/Platform.h"

#include <cassert>
#include <system/Types.h>

#include <system/stb_ds.h>

namespace hpl {
static void _vk__descriptorSetAlloc( struct RIDevice_s *device, struct RIDescriptorSetAlloc *alloc ) {
	assert( device->renderer->api == RI_DEVICE_API_VK );
	struct RIProgram::DescriptorSetSlot *programDescriptor = hpl_container_of( alloc, &RIProgram::DescriptorSetSlot::alloc );
  VkDescriptorPoolSize descriptorPoolSize[16] = {};
  size_t descriptorPoolLen = 0;
  if( programDescriptor->samplerMaxNum > 0 )
  	descriptorPoolSize[descriptorPoolLen++] = (VkDescriptorPoolSize){ VK_DESCRIPTOR_TYPE_SAMPLER, (uint32_t)programDescriptor->samplerMaxNum * DESCRIPTOR_MAX_SIZE };
  if( programDescriptor->constantBufferMaxNum > 0 )
  	descriptorPoolSize[descriptorPoolLen++] = (VkDescriptorPoolSize){ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)programDescriptor->constantBufferMaxNum * DESCRIPTOR_MAX_SIZE };
  if( programDescriptor->dynamicConstantBufferMaxNum > 0 )
  	descriptorPoolSize[descriptorPoolLen++] = (VkDescriptorPoolSize){ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, (uint32_t)programDescriptor->dynamicConstantBufferMaxNum * DESCRIPTOR_MAX_SIZE };
  if( programDescriptor->textureMaxNum > 0 )
  	descriptorPoolSize[descriptorPoolLen++] = (VkDescriptorPoolSize){ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, (uint32_t)programDescriptor->textureMaxNum * DESCRIPTOR_MAX_SIZE };
  if( programDescriptor->storageTextureMaxNum > 0 )
  	descriptorPoolSize[descriptorPoolLen++] = (VkDescriptorPoolSize){ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, (uint32_t)programDescriptor->storageTextureMaxNum * DESCRIPTOR_MAX_SIZE };
  if( programDescriptor->bufferMaxNum > 0 )
  	descriptorPoolSize[descriptorPoolLen++] = (VkDescriptorPoolSize){ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, (uint32_t)programDescriptor->bufferMaxNum * DESCRIPTOR_MAX_SIZE };
  if( programDescriptor->storageBufferMaxNum > 0 )
  	descriptorPoolSize[descriptorPoolLen++] = (VkDescriptorPoolSize){ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, (uint32_t)programDescriptor->storageBufferMaxNum * DESCRIPTOR_MAX_SIZE };
  if( programDescriptor->structuredBufferMaxNum > 0 || programDescriptor->storageStructuredBufferMaxNum > 0 )
  	descriptorPoolSize[descriptorPoolLen++] = (VkDescriptorPoolSize){
  		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)programDescriptor->structuredBufferMaxNum * DESCRIPTOR_MAX_SIZE + (uint32_t)programDescriptor->storageStructuredBufferMaxNum * DESCRIPTOR_MAX_SIZE };
  if( programDescriptor->accelerationStructureMaxNum > 0 )
  	descriptorPoolSize[descriptorPoolLen++] = (VkDescriptorPoolSize){ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, (uint32_t)programDescriptor->accelerationStructureMaxNum * DESCRIPTOR_MAX_SIZE };
  assert( descriptorPoolLen < RRAY_COUNT( descriptorPoolSize ) );
  const VkDescriptorPoolCreateInfo info = {
  	VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, DESCRIPTOR_MAX_SIZE, (uint32_t)descriptorPoolLen, descriptorPoolSize };
  struct RIDescriptorPoolAllocSlot poolSlot = {};
  VK_WrapResult( vkCreateDescriptorPool( device->vk.device, &info, NULL, &poolSlot.vk.handle ) );
  arrpush( alloc->pools, poolSlot );
  for( size_t i = 0; i < DESCRIPTOR_MAX_SIZE; i++ ) {
  	struct RIDescriptorSetSlot* slot = alloc_descriptor_set_slot( alloc );
  	VkDescriptorSetAllocateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
  	info.pNext = NULL;
  	info.descriptorPool = poolSlot.vk.handle;
  	info.descriptorSetCount = 1;
  	assert( programDescriptor->vk.setLayout != VK_NULL_HANDLE );
  	info.pSetLayouts = &programDescriptor->vk.setLayout;
  	VK_WrapResult( vkAllocateDescriptorSets( device->vk.device, &info, &slot->vk.handle ) );
  	arrpush( alloc->reserved_slots, slot );
  }
}

void RIProgram::add_pipeline(struct RIDevice_s *device, hash_t hash,
                VkGraphicsPipelineCreateInfo pipelineCreateInfo) {
  auto it = pipeline.find(hash);
  assert(it != pipeline.end());

  uint32_t numModules = 0;
  VkShaderModule modules[4] = {0};
  VkPipelineShaderStageCreateInfo stageCreateInfo[4] = {};
  if (shader_bin[PROGRAM_STAGE_VERTEX].buf.size() > 0 &&
      shader_bin[PROGRAM_STAGE_FRAGMENT].buf.size() > 0) {
    pipelineCreateInfo.stageCount = 2;
    const VkShaderModuleCreateInfo vertModuleCreateInfo = {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        NULL,
        (VkShaderModuleCreateFlags)0,
        (size_t)shader_bin[PROGRAM_STAGE_VERTEX].buf.size(),
        (const uint32_t *)shader_bin[PROGRAM_STAGE_VERTEX].buf.data(),
    };
    vkCreateShaderModule(device->vk.device, &vertModuleCreateInfo, NULL,
                         &modules[numModules]);
    stageCreateInfo[0] = (VkPipelineShaderStageCreateInfo){ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageCreateInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT,
    stageCreateInfo[0].module = modules[numModules],
    stageCreateInfo[0].pName = "main";
    numModules++;

    const VkShaderModuleCreateInfo fragModuleCreateInfo = {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        NULL,
        (VkShaderModuleCreateFlags)0,
        (size_t)shader_bin[PROGRAM_STAGE_FRAGMENT].buf.size(),
        (const uint32_t *)shader_bin[PROGRAM_STAGE_FRAGMENT].buf.data(),
    };
    vkCreateShaderModule(device->vk.device, &fragModuleCreateInfo, NULL,
                         &modules[numModules]);
    stageCreateInfo[1] = (VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageCreateInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stageCreateInfo[1].module = modules[numModules];
    stageCreateInfo[1].pName = "main";
    numModules++;
  } else {
    assert(false && "failed to resolve bin");
  }
  pipelineCreateInfo.pStages = stageCreateInfo;
  pipelineCreateInfo.basePipelineIndex = -1;
  pipelineCreateInfo.layout = impl.vk.pipeline_layout;
  VK_WrapResult(vkCreateGraphicsPipelines(device->vk.device, VK_NULL_HANDLE, 1,
                                          &pipelineCreateInfo, NULL,
                                          &it->second.vk.handle));

  for (size_t i = 0; i < numModules; i++) {
    vkDestroyShaderModule(device->vk.device, modules[i], NULL);
  }
}


const struct RIProgram::BindingReflection* RIProgram::find_reflection(const struct DescriptorBindingID& handle) {
  for(auto& ref : binding_reflection) {
    if(ref.hash == handle.hash) {
      return &ref;
    }
  }
  return NULL;
}

std::vector<char> RIProgram::load_shader_stage(cFileSearcher *searcher, const tString& asName) {
  std::vector<char> result = {};
	tWString sPath = searcher->GetFilePath(asName);
	if(sPath==_W("")){
		printf("Couldn't find file '%s' in resources\n",asName.c_str());
		return result;
	}
	unsigned int fileSize = cPlatform::GetFileSize(sPath);
	result.resize(fileSize);
	cPlatform::CopyFileToBuffer(sPath,result.data(),fileSize);
  return result;
}

RIProgram& RIProgram::operator=(RIProgram&& prog) {
  reflection_len = prog.reflection_len;
  vertex_input_mask = prog.vertex_input_mask;
  has_push_constants = prog.has_push_constants;
  program_descriptors = std::move(prog.program_descriptors);
  shader_bin = std::move(prog.shader_bin);
  pipeline = std::move(prog.pipeline);
  binding_reflection = std::move(prog.binding_reflection);
  memcpy(&impl, &prog.impl, sizeof(RIProgram::__impl));
  return *this;
}

RIProgram::RIProgram(RIProgram&& prog):
  reflection_len(prog.reflection_len),
  vertex_input_mask(prog.vertex_input_mask),
  has_push_constants(prog.has_push_constants),
  program_descriptors(std::move(prog.program_descriptors)),
  shader_bin(std::move(prog.shader_bin)),
  pipeline(std::move(prog.pipeline)),
  binding_reflection(std::move(prog.binding_reflection)) {
  memcpy(&impl, &prog.impl, sizeof(RIProgram::__impl));
}

RIProgram RIProgram::create(std::span<ModuleStage> moduleInit) {
  RIProgram program = RIProgram();
  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings[DESCRIPTOR_SET_MAX] ;
  std::vector<VkDescriptorBindingFlags> descriptorBindingFlags[DESCRIPTOR_SET_MAX];
  VkDescriptorSetLayout setLayouts[DESCRIPTOR_SET_MAX] = {0};
  VkPushConstantRange pushConstantRange = {0};
  std::vector<SpvReflectDescriptorSet *> reflectionDescSets;

  for (auto &init : moduleInit) {
    auto *bin = &program.shader_bin[init.stage];
    bin->buf.insert(bin->buf.begin(), init.data.begin(), init.data.end());
    SpvReflectShaderModule module = {};
    SpvReflectResult result = spvReflectCreateShaderModule(
        init.data.size(), init.data.data(), &module);
    assert(result == SPV_REFLECT_RESULT_SUCCESS);
    {
      uint32_t pushConstantCount = 0;
      result = spvReflectEnumeratePushConstantBlocks(&module,
                                                     &pushConstantCount, NULL);
      assert(result == SPV_REFLECT_RESULT_SUCCESS);
      program.has_push_constants |= (pushConstantCount > 0);
      if (program.has_push_constants > 0) {
        if (pushConstantCount > 1) {
          printf("Push constant count is greater than 1, only supporting 1 "
                 "push constant\n");
          break;
        }
        pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
        SpvReflectBlockVariable *reflectionBlockVariables[1] = {0};

        result = spvReflectEnumeratePushConstantBlocks(
            &module, &pushConstantCount, reflectionBlockVariables);
        assert(result == SPV_REFLECT_RESULT_SUCCESS);
        pushConstantRange.size = reflectionBlockVariables[0]->size;
        program.impl.vk.pushConstant.size = reflectionBlockVariables[0]->size;
        switch (init.stage) {
        case PROGRAM_STAGE_VERTEX:
          pushConstantRange.stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
          program.impl.vk.pushConstant.shader_stage_flags |= VK_SHADER_STAGE_VERTEX_BIT;
          break;
        case PROGRAM_STAGE_FRAGMENT:
          pushConstantRange.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
          program.impl.vk.pushConstant.shader_stage_flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
          break;
        default:
          assert(false);
          break;
        }
      }
    }

    if (init.stage == PROGRAM_STAGE_VERTEX) {
      for (size_t i = 0; i < module.input_variable_count; i++) {
        program.vertex_input_mask |= (1 << module.input_variables[i]->location);
      }
    }

    uint32_t reflectionDescriptorCount = 0;
    result = spvReflectEnumerateDescriptorSets(
        &module, &reflectionDescriptorCount, NULL);
    assert(result == SPV_REFLECT_RESULT_SUCCESS);
    reflectionDescSets.resize(reflectionDescriptorCount);
    result = spvReflectEnumerateDescriptorSets(
        &module, &reflectionDescriptorCount, reflectionDescSets.data());
    assert(result == SPV_REFLECT_RESULT_SUCCESS);
    for (size_t i_set = 0; i_set < reflectionDescriptorCount; i_set++) {
      const SpvReflectDescriptorSet *spv_reflection = reflectionDescSets[i_set];
      assert(reflection->set < ARRAY_COUNT(program->programDescriptors));
      struct DescriptorSetSlot *program_desc =
          &program.program_descriptors[spv_reflection->set];
      program_desc->alloc.descriptor_alloc_handle = _vk__descriptorSetAlloc;
      program_desc->alloc.framesInFlight = RI_NUMBER_FRAMES_FLIGHT;
      for (size_t i_binding = 0; i_binding < spv_reflection->binding_count;
           i_binding++) {
        const SpvReflectDescriptorBinding *reflectionBinding =
            spv_reflection->bindings[i_binding];
        assert(reflection->set < R_DESCRIPTOR_SET_MAX);
        assert(reflectionBinding->array.dims_count <=
               1); // not going to handle multi-dim arrays
        struct BindingReflection reflc = {};
        reflc.hash = create_descriptor_binding_id(reflectionBinding->name).hash;
        reflc.set = reflectionBinding->set;
        reflc.baseRegisterIndex = reflectionBinding->binding;
        reflc.isArray = reflectionBinding->count > 1;
        reflc.dimCount = std::max<uint16_t>(1, reflectionBinding->count);

        VkDescriptorSetLayoutBinding *layoutBinding = NULL;
        VkDescriptorBindingFlags *bindingFlags = NULL;
        for (size_t i = 0;
             i < descriptorSetLayoutBindings[spv_reflection->set].size();
             i++) {
          if (descriptorSetLayoutBindings[spv_reflection->set][i].binding ==
              reflectionBinding->binding) {
            layoutBinding =
                &descriptorSetLayoutBindings[spv_reflection->set][i];
            bindingFlags = &descriptorBindingFlags[spv_reflection->set][i];
          }
        }

        if (!layoutBinding) {
          VkDescriptorSetLayoutBinding bindings = {0};
          VkDescriptorBindingFlags flags = 0;
          descriptorSetLayoutBindings[spv_reflection->set].push_back(bindings);
          descriptorBindingFlags[spv_reflection->set].push_back(flags);
          layoutBinding =
              &descriptorSetLayoutBindings[spv_reflection->set][
              descriptorSetLayoutBindings[spv_reflection->set].size() - 1];
          bindingFlags = &descriptorBindingFlags[spv_reflection->set][
                         descriptorBindingFlags[spv_reflection->set].size() -
                         1];
        }

        if (reflc.isArray) {
          (*bindingFlags) = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
        }

        const uint32_t bindingCount =
            std::max<uint32_t>(reflectionBinding->count, 1);
        layoutBinding->binding = reflectionBinding->binding;
        layoutBinding->descriptorCount = bindingCount;
        switch (init.stage) {
        case PROGRAM_STAGE_VERTEX:
          layoutBinding->stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
          break;
        case PROGRAM_STAGE_FRAGMENT:
          layoutBinding->stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
          break;
        default:
          assert(false);
          break;
        }
        switch (reflectionBinding->descriptor_type) {
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
          layoutBinding->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
          program_desc->samplerMaxNum += bindingCount;
          break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
          layoutBinding->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
          program_desc->textureMaxNum += bindingCount;
          break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
          layoutBinding->descriptorType =
              VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
          program_desc->bufferMaxNum += bindingCount;
          break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
          layoutBinding->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
          program_desc->storageTextureMaxNum += bindingCount;
          break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
          layoutBinding->descriptorType =
              VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
          program_desc->storageBufferMaxNum += bindingCount;
          break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
          layoutBinding->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
          program_desc->constantBufferMaxNum += bindingCount;
          break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
          layoutBinding->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
          program_desc->structuredBufferMaxNum += bindingCount;
          break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
        case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
          assert(false);
          break;
        }
        program.binding_reflection.push_back(reflc);
      }
    }
    return program;
  }
}

} // namespace hpl
