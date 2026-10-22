#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "bluevk/context.hpp"

namespace bvk {

// Small Vulkan helpers: pipelines from a short description, images with
// their views, render passes and framebuffers, descriptors and barriers.

// A graphics pipeline: everything a renderer's pipelines vary, with the
// rest fixed — viewport and scissor dynamic, one viewport, counter-clockwise
// front faces.
struct PipelineDesc {
  VkShaderModule vs = VK_NULL_HANDLE;
  VkShaderModule fs = VK_NULL_HANDLE;  // none: depth only
  const VkSpecializationInfo* fsSpec = nullptr;
  // Vertex input (none: the shader makes its vertices from the index).
  std::vector<VkVertexInputBindingDescription> bindings;
  std::vector<VkVertexInputAttributeDescription> attributes;
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPolygonMode polygon = VK_POLYGON_MODE_FILL;
  VkCullModeFlags cull = VK_CULL_MODE_NONE;
  float lineWidth = 1.0f;
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
  bool depthTest = false, depthWrite = false;
  VkCompareOp depthCompare = VK_COMPARE_OP_GREATER;  // reversed-Z
  // One per color attachment of the subpass (none: no color).
  std::vector<VkPipelineColorBlendAttachmentState> blends;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkRenderPass pass = VK_NULL_HANDLE;
};
// VK_NULL_HANDLE (and logged) when it fails.
VkPipeline makeGraphicsPipeline(Ctx&, const PipelineDesc&);

// Color attachment states: written as is; alpha-blended (src over dst);
// added on; multiplied in (dst * src).
VkPipelineColorBlendAttachmentState blendNone();
VkPipelineColorBlendAttachmentState blendAlpha();
VkPipelineColorBlendAttachmentState blendAdd();
VkPipelineColorBlendAttachmentState blendMultiply();

// A device-local image and one view of it.
struct Image {
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation alloc = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};
struct ImageDesc {
  VkExtent2D extent{};
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  uint32_t mips = 1, layers = 1;
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
};
// The image and a view of all of it (2D, or 2D array with layers > 1).
bool createImage(Ctx&, const ImageDesc&, Image& out);
// A view of mips [mip, mip + mipCount) and layers [layer, layer + layerCount).
VkImageView createImageView(Ctx&, VkImage, VkFormat, VkImageAspectFlags aspect, uint32_t mip = 0,
                            uint32_t mipCount = 1, uint32_t layer = 0, uint32_t layerCount = 1,
                            VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D);
void destroyImage(Ctx&, Image&);

// An attachment from UNDEFINED to `finalLayout`, its stencil not kept.
VkAttachmentDescription attachment(VkFormat, VkAttachmentLoadOp, VkAttachmentStoreOp, VkImageLayout finalLayout,
                                   VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
// One subpass over `colors` color attachments and an optional depth one
// (the last attachment), with the given external dependencies.
VkRenderPass makeRenderPass(Ctx&, const std::vector<VkAttachmentDescription>& atts, uint32_t colors, bool depth,
                            const std::vector<VkSubpassDependency>& deps);
VkFramebuffer makeFramebuffer(Ctx&, VkRenderPass, const std::vector<VkImageView>& views, VkExtent2D size);

// A compute pipeline from one module (entry point "main").
VkPipeline makeComputePipeline(Ctx&, VkShaderModule, VkPipelineLayout);
// The same, straight from embedded SPIR-V (the module is destroyed after).
VkPipeline makeComputePipeline(Ctx&, const uint32_t* code, size_t bytes, VkPipelineLayout);

// A descriptor set layout from (binding, type) pairs, every binding visible
// to `stages`.
VkDescriptorSetLayout makeSetLayout(Ctx&, const std::vector<std::pair<uint32_t, VkDescriptorType>>& bindings,
                                    VkShaderStageFlags stages);
// A set from `pool` (none: Ctx::descPool).
VkDescriptorSet allocSet(Ctx&, VkDescriptorSetLayout, VkDescriptorPool pool = VK_NULL_HANDLE);
void writeBuffer(Ctx&, VkDescriptorSet, uint32_t binding, VkDescriptorType, VkBuffer, VkDeviceSize size = VK_WHOLE_SIZE);
void writeImage(Ctx&, VkDescriptorSet, uint32_t binding, VkDescriptorType, VkImageView, VkImageLayout,
                VkSampler sampler = VK_NULL_HANDLE);

// Groups of `group` invocations covering `n` items.
inline uint32_t groupsFor(uint64_t n, uint32_t group = 256) { return static_cast<uint32_t>((n + group - 1) / group); }

// A global memory barrier: everything `src` wrote, visible to `dst`.
void memoryBarrier(VkCommandBuffer, VkPipelineStageFlags src, VkAccessFlags srcAccess, VkPipelineStageFlags dst,
                   VkAccessFlags dstAccess);
// Compute writes, visible to the next compute dispatch.
inline void computeBarrier(VkCommandBuffer cmd) {
  memoryBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}
// One image's layout change (whole image, one mip/layer).
void imageBarrier(VkCommandBuffer, VkImage, VkImageAspectFlags, VkImageLayout from, VkImageLayout to,
                  VkPipelineStageFlags src, VkAccessFlags srcAccess, VkPipelineStageFlags dst, VkAccessFlags dstAccess);

}  // namespace bvk
