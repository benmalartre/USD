//
// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/base/tf/diagnostic.h"

#include <algorithm>
#include <cstdio>

#include "pxr/imaging/hgiVulkan/buffer.h"
#include "pxr/imaging/hgiVulkan/capabilities.h"
#include "pxr/imaging/hgiVulkan/conversions.h"
#include "pxr/imaging/hgiVulkan/device.h"
#include "pxr/imaging/hgiVulkan/diagnostic.h"
#include "pxr/imaging/hgiVulkan/resourceBindings.h"
#include "pxr/imaging/hgiVulkan/sampler.h"
#include "pxr/imaging/hgiVulkan/texture.h"
#include "pxr/imaging/hgiVulkan/accelerationStructure.h"

PXR_NAMESPACE_OPEN_SCOPE

namespace {
    static const uint8_t _descriptorSetCnt = 1;
}

static VkDescriptorSetLayout
_CreateDescriptorSetLayout(
    HgiVulkanDevice* device,
    std::vector<VkDescriptorSetLayoutBinding> const& bindings,
    std::string const& debugName)
{
    // Create descriptor
    VkDescriptorSetLayoutCreateInfo setCreateInfo =
        {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setCreateInfo.bindingCount = (uint32_t) bindings.size();
    setCreateInfo.pBindings = bindings.data();
    setCreateInfo.pNext = nullptr;

    VkDescriptorSetLayout layout = nullptr;
    HGIVULKAN_VERIFY_VK_RESULT(
        vkCreateDescriptorSetLayout(
            device->GetVulkanDevice(),
            &setCreateInfo,
            HgiVulkanAllocator(),
            &layout)
    );

    // Debug label
    if (!debugName.empty()) {
        std::string debugLabel = "DescriptorSetLayout " + debugName;
        HgiVulkanSetDebugName(
            device,
            (uint64_t)layout,
            VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
            debugLabel.c_str());
    }

    return layout;
}

HgiVulkanResourceBindings::HgiVulkanResourceBindings(
    HgiVulkanDevice* device,
    HgiResourceBindingsDesc const& desc)
    : HgiResourceBindings(desc)
    , _device(device)
    , _inflightBits(0)
    , _vkDescriptorPool(nullptr)
    , _vkDescriptorSetLayout(nullptr)
    , _vkDescriptorSet(nullptr)
{
    fprintf(stderr, "[HgiVulkanResourceBindings] CTOR MARKER 2026-07-03-poolfix: "
        "textures=%zu buffers=%zu accelStructs=%zu\n",
        desc.textures.size(), desc.buffers.size(),
        desc.accelerationStructures.size());
    fflush(stderr);
    // Initialize the pool sizes for each descriptor type we support
    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.resize(HgiBindResourceTypeCount);

    for (size_t i=0; i<HgiBindResourceTypeCount; i++) {
        HgiBindResourceType bt = HgiBindResourceType(i);
        VkDescriptorPoolSize p;
        p.descriptorCount = 0;
        p.type = HgiVulkanConversions::GetDescriptorType(bt);
        poolSizes[i] = p;
    }

    // OpenGL (and Metal) have separate bindings for each buffer and image type.
    // Ubo, ssbo, sampler2D, image all start at bindingIndex 0. So we expect
    // that Hgi clients may specify OpenGL style bindingIndex for each.
    // In Vulkan, bindingIndices are shared (incremented) across all resources.
    // We could split all four into a separate descriptorSet and set the
    // slot=XX in the shader. Instead we keep all resources in one
    // descriptor set and increment all Hgi binding indices here.
    // This assumes that Hgi codeGen does the same for vulkan glsl.

    // For non-bindless buffers in Storm, uniform and storage buffers share a
    // binding index counter, while textures have their own binding index
    // counter. Thus for Vulkan, we adjust the texture bind indices to start
    // after the last buffer bind index.
    // E.g. If HgiResourceBindingDesc indicates the following binding indices:
    // UBO1: 0, SSBO1: 1, SSB02: 2, TEX1: 0, TEX2: 1, here we change that to:
    // UBO1: 0, SSBO1: 1, SSB02: 2, TEX1: 3, TEX2: 4.

    uint32_t textureBindIndexStart = 0;

    // XXX We need to overspecify the stage usage here so we can match the
    // VkDescriptorSetLayout that is created with spirv-reflect for the
    // graphics and compute pipelines.
    VkShaderStageFlags const bufferShaderStageFlags =
        HgiVulkanConversions::GetShaderStages(
            HgiShaderStageVertex | HgiShaderStageTessellationControl |
            HgiShaderStageTessellationEval | HgiShaderStageGeometry |
            HgiShaderStageFragment);
    VkShaderStageFlags const textureShaderStageFlags =
        HgiVulkanConversions::GetShaderStages(
            HgiShaderStageGeometry | HgiShaderStageFragment);

    // Ray tracing clients (e.g. Aurora's HGI/Vulkan ray tracing backend) specify
    // exact stage usage (HgiShaderStageRayGen/ClosestHit/Miss/...) that has nothing
    // to do with the graphics/compute pipelines the overspecification above exists
    // for. Falling through to the hardcoded bufferShaderStageFlags/
    // textureShaderStageFlags for these produces a descriptor set layout with
    // completely wrong VkShaderStageFlags (e.g. GEOMETRY|FRAGMENT instead of
    // RAYGEN_BIT_KHR), which is incompatible with a ray tracing pipeline layout at
    // vkCmdBindDescriptorSets/vkCmdTraceRaysKHR time (VUID-vkCmdTraceRaysKHR-None-08600).
    static const HgiShaderStage kRayTracingStages = HgiShaderStageRayGen |
        HgiShaderStageAnyHit | HgiShaderStageClosestHit | HgiShaderStageMiss |
        HgiShaderStageIntersection | HgiShaderStageCallable;

    // Create DescriptorSetLayout to describe resource bindings.
    //
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    uint32_t bufferBindIndexStart = 0;
    for (HgiAccelerationStructureBindDesc const& a : desc.accelerationStructures) {
        VkDescriptorSetLayoutBinding d = {};
        d.binding = a.bindingIndex;
        d.descriptorType =
            HgiVulkanConversions::GetDescriptorType(a.resourceType);
        d.descriptorCount = (uint32_t)a.accelerationStructures.size();
        // Pool capacity must reserve one slot per actual descriptor, not one
        // per binding — a binding can be an array of N descriptors.
        poolSizes[a.resourceType].descriptorCount += d.descriptorCount;
        d.stageFlags = HgiVulkanConversions::GetShaderStages(a.stageUsage);
        d.pImmutableSamplers = nullptr;
        bindings.push_back(std::move(d));

        // NOTE: intentionally NOT bumping bufferBindIndexStart from acceleration
        // structure binding indices. This auto-offset scheme exists to merge
        // OpenGL-style per-resource-category binding indices (which commonly start
        // at 0 in each category and collide with each other) into one shared Vulkan
        // binding space, driven by codegen that expects this same adjustment. Ray
        // tracing clients (e.g. Aurora's HGI/Vulkan ray tracing backend) assign
        // their own globally-unique absolute Vulkan binding indices directly,
        // matching hand-authored/transpiled shader source and a separately
        // maintained VkPipelineLayout that does not apply this offset. Letting an
        // acceleration structure's presence shift buffer/texture binding indices
        // here silently desyncs the descriptor set layout from that pipeline
        // layout, since the pipeline layout has no way to know about this shift.
    }

    // Buffers
    for (HgiBufferBindDesc const& b : desc.buffers) {
        VkDescriptorSetLayoutBinding d = {};
        d.binding = bufferBindIndexStart + b.bindingIndex;
        d.descriptorType =
            HgiVulkanConversions::GetDescriptorType(b.resourceType);
        d.descriptorCount = (uint32_t) b.buffers.size();
        // Pool capacity must reserve one slot per actual descriptor, not one
        // per binding — a binding can be an array of N descriptors.
        poolSizes[b.resourceType].descriptorCount += d.descriptorCount;
        d.stageFlags = (b.stageUsage == HgiShaderStageCompute ||
                (b.stageUsage & kRayTracingStages)) ?
            HgiVulkanConversions::GetShaderStages(b.stageUsage) :
            bufferShaderStageFlags;
        d.pImmutableSamplers = nullptr;
        bindings.push_back(std::move(d));

        // NOTE: intentionally NOT bumping textureBindIndexStart from buffer binding
        // indices, for the same reason bufferBindIndexStart is no longer bumped by
        // acceleration structures above. Aurora's buffers already use large raw
        // absolute binding numbers (e.g. 10, 11), so this would have shifted every
        // texture binding by 12+, silently dropping binding=1 (the ray tracing
        // output image) from the descriptor set entirely and colliding it with an
        // unrelated AOV binding — exactly the "SkipBinding on a null/missing
        // binding" crash this was chasing.
    }

    // Textures
    for (HgiTextureBindDesc const& t : desc.textures) {
        // Descriptor count made of textures and samplers.
        size_t descriptorCount = std::max(t.textures.size(), t.samplers.size());
        VkDescriptorSetLayoutBinding d = {};
        d.binding = textureBindIndexStart + t.bindingIndex;
        d.descriptorType =
            HgiVulkanConversions::GetDescriptorType(t.resourceType);
        d.descriptorCount = (uint32_t)descriptorCount;
        // Pool capacity must reserve one slot per actual descriptor, not one
        // per binding — a binding can be an array of N descriptors (e.g. Aurora's
        // fixed-size instance texture array, kMaxTextures=64, bound as a single
        // HgiTextureBindDesc with 64 entries). Previously this only reserved 1 slot
        // per HgiTextureBindDesc regardless of array size, so vkUpdateDescriptorSets
        // would write far more descriptors than the pool had capacity for — silently
        // corrupting adjacent pool/descriptor memory without validation layers
        // enabled, and crashing inside the validation layer's own bookkeeping
        // (vvl::DescriptorSet::PerformWriteUpdate) when they are.
        poolSizes[t.resourceType].descriptorCount += d.descriptorCount;
        d.stageFlags = (t.stageUsage == HgiShaderStageCompute ||
                (t.stageUsage & kRayTracingStages)) ?
            HgiVulkanConversions::GetShaderStages(t.stageUsage) :
            textureShaderStageFlags;
        d.pImmutableSamplers = nullptr;
        bindings.push_back(std::move(d));
    }

    // Sort by binding number before creating the layout. The Vulkan spec does not
    // require any particular order for VkDescriptorSetLayoutCreateInfo::pBindings,
    // but this vector is built by appending acceleration-structure, then buffer,
    // then texture entries in whatever order the caller supplied bindingIndex
    // values (e.g. Aurora's ray tracing bindings arrive as buffers=[2,4,5,10,3,11],
    // textures=[1,20,6,7,8,9,12..17] — not ascending). Sorting removes any
    // dependency on validation-layer/driver internals assuming ascending order for
    // their own binding-number lookup structures.
    std::sort(bindings.begin(), bindings.end(),
        [](VkDescriptorSetLayoutBinding const& a, VkDescriptorSetLayoutBinding const& b) {
            return a.binding < b.binding;
        });

    // Create descriptor set layout
    _vkDescriptorSetLayout =
        _CreateDescriptorSetLayout(_device, bindings, _descriptor.debugName);

    //
    // Create the descriptor pool.
    //
    // XXX For now each resource bindings gets its own pool to allocate its
    // descriptor sets from to simplify multi-threading support.
    for (size_t i=poolSizes.size(); i-- > 0;) {
        // Vulkan validation will complain if any descriptorCount is 0.
        // Instead of removing them we set a minimum of 1. An empty poolSize
        // will not let us create the pool, which prevents us from creating
        // the descriptorSets.
        poolSizes[i].descriptorCount=std::max(poolSizes[i].descriptorCount, 1u);
    }

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = _descriptorSetCnt;
    pool_info.poolSizeCount = (uint32_t) poolSizes.size();
    pool_info.pPoolSizes = poolSizes.data();

    HGIVULKAN_VERIFY_VK_RESULT(
        vkCreateDescriptorPool(
            _device->GetVulkanDevice(),
            &pool_info,
            HgiVulkanAllocator(),
            &_vkDescriptorPool)
    );

    // Debug label
    if (!_descriptor.debugName.empty()) {
        std::string debugLabel = "Descriptor Pool " + _descriptor.debugName;
        HgiVulkanSetDebugName(
            device,
            (uint64_t)_vkDescriptorPool,
            VK_OBJECT_TYPE_DESCRIPTOR_POOL,
            debugLabel.c_str());
    }

    //
    // Create Descriptor Set
    //
    VkDescriptorSetAllocateInfo allocateInfo =
        {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};

    allocateInfo.descriptorPool = _vkDescriptorPool;
    allocateInfo.descriptorSetCount = _descriptorSetCnt;
    allocateInfo.pSetLayouts = &_vkDescriptorSetLayout;

    HGIVULKAN_VERIFY_VK_RESULT(
        vkAllocateDescriptorSets(
            _device->GetVulkanDevice(),
            &allocateInfo,
            &_vkDescriptorSet)
    );

    // Debug label
    if (!_descriptor.debugName.empty()) {
        std::string dbgLbl = "Descriptor Set Buffers " + _descriptor.debugName;
        HgiVulkanSetDebugName(
            _device,
            (uint64_t)_vkDescriptorSet,
            VK_OBJECT_TYPE_DESCRIPTOR_SET,
            dbgLbl.c_str());
    }

    //
    // Setup limits for each resource type
    //
    VkPhysicalDeviceProperties const& devProps =
        _device->GetDeviceCapabilities().vkDeviceProperties2.properties;
    VkPhysicalDeviceLimits const& limits = devProps.limits;

    uint32_t bindLimits[HgiBindResourceTypeCount][2] = {
        {HgiBindResourceTypeSampler,
            limits.maxPerStageDescriptorSamplers},
        {HgiBindResourceTypeSampledImage,
            limits.maxPerStageDescriptorSampledImages},
        {HgiBindResourceTypeCombinedSamplerImage,
            limits.maxPerStageDescriptorSampledImages},
        {HgiBindResourceTypeStorageImage,
            limits.maxPerStageDescriptorStorageImages},
        {HgiBindResourceTypeUniformBuffer,
            limits.maxPerStageDescriptorUniformBuffers},
        {HgiBindResourceTypeStorageBuffer,
            limits.maxPerStageDescriptorStorageBuffers},
        {HgiBindResourceTypeTessFactors,
            0}, // unsupported
        {HgiBindResourceTypeAccelerationStructure,
            1024}
    };
    static_assert(HgiBindResourceTypeCount==8, "");

    std::vector<VkWriteDescriptorSet> writeSets;

    //
    // Acceleration Structures
    //

    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> accelerationStructureInfos;
    std::vector < std::vector<VkAccelerationStructureKHR> > accelerationStructureIds;
    accelerationStructureInfos.resize(desc.accelerationStructures.size());
    accelerationStructureIds.resize(desc.accelerationStructures.size());

    size_t asIdx = 0;
    for (HgiAccelerationStructureBindDesc const& asDesc : desc.accelerationStructures) {
        uint32_t& limit = bindLimits[asDesc.resourceType][1];
        if (!TF_VERIFY(limit > 0, "Maximum size array-of-acceleration structures exceeded")) {
            break;
        }
        limit -= 1;

        // Each buffer can be an array of buffers (usually one)
        for (size_t i = 0; i < asDesc.accelerationStructures.size(); i++) {
            HgiAccelerationStructureHandle const& asHandle = asDesc.accelerationStructures[i];
            HgiVulkanAccelerationStructure* as =
                static_cast<HgiVulkanAccelerationStructure*>(asHandle.Get());
            if (!TF_VERIFY(as)) continue;

            accelerationStructureIds[asIdx].push_back((VkAccelerationStructureKHR)as->GetRawResource());
        }

        VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
        asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        asInfo.accelerationStructureCount = accelerationStructureIds[asIdx].size();
        asInfo.pAccelerationStructures = accelerationStructureIds[asIdx].data();
        accelerationStructureInfos[asIdx] = asInfo;
        asIdx++;

    }

    size_t asInfoOffset = 0;
    asIdx = 0;
    for (HgiAccelerationStructureBindDesc const& asDesc : desc.accelerationStructures) {
        VkWriteDescriptorSet writeSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        writeSet.dstBinding = asDesc.bindingIndex;
        writeSet.pNext = &accelerationStructureInfos[asIdx];
        writeSet.dstArrayElement = 0;
        writeSet.descriptorCount = (uint32_t)asDesc.accelerationStructures.size(); // 0 ok
        writeSet.dstSet = _vkDescriptorSet;
        writeSet.pBufferInfo = nullptr;
        writeSet.pImageInfo = nullptr;
        writeSet.pTexelBufferView = nullptr;
        writeSet.descriptorType =
            HgiVulkanConversions::GetDescriptorType(asDesc.resourceType);
        writeSets.push_back(std::move(writeSet));
        asInfoOffset += asDesc.accelerationStructures.size();
        asIdx++;
    }



    //
    // Buffers
    //

    std::vector<VkDescriptorBufferInfo> bufferInfos;
    bufferInfos.reserve(desc.buffers.size());

    for (HgiBufferBindDesc const& bufDesc : desc.buffers) {
        uint32_t & limit = bindLimits[bufDesc.resourceType][1];
        if (!TF_VERIFY(limit>0, "Maximum size array-of-buffers exceeded")) {
            break;
        }
        limit -= 1;

        TF_VERIFY(bufDesc.buffers.size() == bufDesc.offsets.size());

        // Each buffer can be an array of buffers (usually one)
        for (size_t i=0; i<bufDesc.buffers.size(); i++) {
            HgiBufferHandle const& bufHandle = bufDesc.buffers[i];
            HgiVulkanBuffer* buf =
                static_cast<HgiVulkanBuffer*>(bufHandle.Get());
            if (!TF_VERIFY(buf)) continue;
            VkDescriptorBufferInfo bufferInfo;
            bufferInfo.buffer = buf->GetVulkanBuffer();
            bufferInfo.offset = bufDesc.offsets[i];
            bufferInfo.range = VK_WHOLE_SIZE;
            bufferInfos.push_back(std::move(bufferInfo));
        }
    }

    size_t bufInfoOffset = 0;
    for (HgiBufferBindDesc const& bufDesc : desc.buffers) {
        VkWriteDescriptorSet writeSet= {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        // Must match the offset applied when building the layout binding above
        // (d.binding = bufferBindIndexStart + b.bindingIndex) — this was previously
        // missing the bufferBindIndexStart offset, so the write targeted a Vulkan
        // binding index that didn't match what the descriptor set layout actually
        // declared (e.g. off by 1 whenever an acceleration structure is also bound,
        // since that shifts bufferBindIndexStart to 1).
        writeSet.dstBinding = bufferBindIndexStart + bufDesc.bindingIndex;
        writeSet.dstArrayElement = 0;
        writeSet.descriptorCount = (uint32_t) bufDesc.buffers.size(); // 0 ok
        writeSet.dstSet = _vkDescriptorSet;
        writeSet.pBufferInfo = bufferInfos.data() + bufInfoOffset;
        writeSet.pImageInfo = nullptr;
        writeSet.pTexelBufferView = nullptr;
        writeSet.descriptorType =
            HgiVulkanConversions::GetDescriptorType(bufDesc.resourceType);
        writeSets.push_back(std::move(writeSet));
        bufInfoOffset += bufDesc.buffers.size();
    }

    //
    // Textures
    //

    std::vector<VkDescriptorImageInfo> imageInfos;
    {
        // Reserve capacity for the true total descriptor count, not the number of
        // texture *bindings* — a single binding can be an array of N descriptors
        // (e.g. Aurora's instance texture array, kMaxTextures=64, in one
        // HgiTextureBindDesc). Under-reserving here forces reallocation mid-growth;
        // reserve() exactly once upfront so imageInfos.data() never moves once any
        // pointer into it is taken below.
        size_t totalImageInfoCount = 0;
        for (HgiTextureBindDesc const& t : desc.textures) {
            totalImageInfoCount += std::max(t.textures.size(), t.samplers.size());
        }
        imageInfos.reserve(totalImageInfoCount);
    }

    for (HgiTextureBindDesc const& texDesc : desc.textures) {

        uint32_t & limit = bindLimits[texDesc.resourceType][1];
        if (!TF_VERIFY(limit>0, "Maximum array-of-texture/samplers exceeded")) {
            break;
        }
        limit -= 1;

        // Each texture can be an array of textures and samplers.
        size_t descriptorCount = std::max(texDesc.textures.size(), texDesc.samplers.size());
        for (size_t i=0; i< descriptorCount; i++) {

            HgiVulkanTexture* tex = nullptr;
            if (i < texDesc.textures.size()) {
                const HgiTextureHandle& texHandle = texDesc.textures[i];
                tex = static_cast<HgiVulkanTexture*>(texHandle.Get());
                // Do NOT 'continue' here: this loop's iteration count must exactly
                // match descriptorCount (used below to size writeSet.descriptorCount
                // and to compute pImageInfo offsets for subsequent bindings). Skipping
                // the push_back on a null texture silently shrinks imageInfos below
                // descriptorCount, causing vkUpdateDescriptorSets to read past the
                // valid range for this binding (and corrupts offsets for every
                // texture binding processed after it). Fall through and push a
                // VK_NULL_HANDLE image entry instead — same as the existing
                // null-sampler handling just below.
                if (!tex) {
                    fprintf(stderr, "[HgiVulkanResourceBindings] NULL TEXTURE at "
                        "binding=%u index=%zu/%zu (descriptorCount=%zu)\n",
                        texDesc.bindingIndex, i, texDesc.textures.size(),
                        descriptorCount);
                    fflush(stderr);
                }
            }

            // Not having a sampler is ok only for StorageImage.
            HgiVulkanSampler* smp = nullptr;
            if (i < texDesc.samplers.size()) {
                HgiSamplerHandle const& smpHandle = texDesc.samplers[i];
                smp = static_cast<HgiVulkanSampler*>(smpHandle.Get());
            }

            VkDescriptorImageInfo imageInfo;
            imageInfo.sampler = smp ? smp->GetVulkanSampler() : nullptr;
            imageInfo.imageLayout = tex ? tex->GetImageLayout() : VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.imageView = tex ? tex->GetImageView() : VK_NULL_HANDLE;
            imageInfos.push_back(std::move(imageInfo));
        }
    }

    size_t texInfoOffset = 0;
    for (HgiTextureBindDesc const& texDesc : desc.textures) {
        size_t descriptorCount = std::max(texDesc.textures.size(), texDesc.samplers.size());

        // For dstBinding we must provided an index in descriptor set.
        // Must be one of the bindings specified in VkDescriptorSetLayoutBinding
        VkWriteDescriptorSet writeSet= {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writeSet.dstBinding = textureBindIndexStart + texDesc.bindingIndex;
        writeSet.dstArrayElement = 0;
        writeSet.descriptorCount = (uint32_t)descriptorCount;
        writeSet.dstSet = _vkDescriptorSet;
        writeSet.pBufferInfo = nullptr;
        writeSet.pImageInfo = imageInfos.data() + texInfoOffset;
        writeSet.pTexelBufferView = nullptr;
        writeSet.descriptorType =
            HgiVulkanConversions::GetDescriptorType(texDesc.resourceType);
        writeSets.push_back(std::move(writeSet));
        texInfoOffset += descriptorCount;
    }

    // Note: this update is immediate. It is not recorded via a command.
    // This means we should only do this if the descriptorSet is not currently
    // in use on GPU. With 'descriptor indexing' extension this has relaxed a
    // little and we are allowed to use vkUpdateDescriptorSets before
    // vkBeginCommandBuffer and after vkEndCommandBuffer, just not during the
    // command buffer recording.
    vkUpdateDescriptorSets(
        _device->GetVulkanDevice(),
        (uint32_t) writeSets.size(),
        writeSets.data(),
        0,        // copy count
        nullptr); // copy_desc
}

HgiVulkanResourceBindings::~HgiVulkanResourceBindings()
{
    vkDestroyDescriptorSetLayout(
        _device->GetVulkanDevice(),
        _vkDescriptorSetLayout,
        HgiVulkanAllocator());

    // Since we have one pool for this resourceBindings we can reset the pool
    // instead of freeing the descriptorSets (vkFreeDescriptorSets).
    vkDestroyDescriptorPool(
        _device->GetVulkanDevice(),
        _vkDescriptorPool,
        HgiVulkanAllocator());
}

void
HgiVulkanResourceBindings::BindResources(
    VkCommandBuffer cb,
    VkPipelineBindPoint bindPoint,
    VkPipelineLayout layout)
{
    // When binding new resources for the currently bound pipeline it may
    // 'disturb' previously bound resources (for a previous pipeline) that
    // are no longer compatible with the layout for the new pipeline.
    // This essentially unbinds the old resources.

    vkCmdBindDescriptorSets(
        cb,
        bindPoint,
        layout,
        0, // firstSet/slot - Hgi does not provide slot index, assume 0.
        _descriptorSetCnt,
        &_vkDescriptorSet,
        0, // dynamicOffset
        nullptr);
}

HgiVulkanDevice*
HgiVulkanResourceBindings::GetDevice() const
{
    return _device;
}

uint64_t &
HgiVulkanResourceBindings::GetInflightBits()
{
    return _inflightBits;
}

PXR_NAMESPACE_CLOSE_SCOPE