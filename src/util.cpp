#include "bluevk/util.hpp"

#include "log.hpp"

namespace bvk {

using detail::log;

VkPipeline makeGraphicsPipeline(Ctx& ctx, const PipelineDesc& d) {
  VkPipelineShaderStageCreateInfo stages[2]{
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, d.vs, "main",
       nullptr},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, d.fs, "main",
       d.fsSpec},
  };

  VkPipelineVertexInputStateCreateInfo vin{};
  vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vin.vertexBindingDescriptionCount = static_cast<uint32_t>(d.bindings.size());
  vin.pVertexBindingDescriptions = d.bindings.data();
  vin.vertexAttributeDescriptionCount = static_cast<uint32_t>(d.attributes.size());
  vin.pVertexAttributeDescriptions = d.attributes.data();

  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = d.topology;

  VkPipelineViewportStateCreateInfo vp{};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = d.polygon;
  rs.cullMode = d.cull;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = d.lineWidth;

  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = d.samples;

  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = d.depthTest ? VK_TRUE : VK_FALSE;
  ds.depthWriteEnable = d.depthWrite ? VK_TRUE : VK_FALSE;
  ds.depthCompareOp = d.depthCompare;

  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = static_cast<uint32_t>(d.blends.size());
  cb.pAttachments = d.blends.data();

  const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dync{};
  dync.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dync.dynamicStateCount = 2;
  dync.pDynamicStates = dyn;

  VkGraphicsPipelineCreateInfo gpi{};
  gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gpi.stageCount = d.fs ? 2 : 1;
  gpi.pStages = stages;
  gpi.pVertexInputState = &vin;
  gpi.pInputAssemblyState = &ia;
  gpi.pViewportState = &vp;
  gpi.pRasterizationState = &rs;
  gpi.pMultisampleState = &ms;
  gpi.pDepthStencilState = &ds;
  gpi.pColorBlendState = &cb;
  gpi.pDynamicState = &dync;
  gpi.layout = d.layout;
  gpi.renderPass = d.pass;

  VkPipeline pipe = VK_NULL_HANDLE;
  if (vkCreateGraphicsPipelines(ctx.dev, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipe) != VK_SUCCESS) {
    log(LogLevel::Error, "vkCreateGraphicsPipelines failed");
    return VK_NULL_HANDLE;
  }
  return pipe;
}

namespace {
constexpr VkColorComponentFlags kRgba =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
}  // namespace

VkPipelineColorBlendAttachmentState blendNone() {
  VkPipelineColorBlendAttachmentState b{};
  b.colorWriteMask = kRgba;
  return b;
}

VkPipelineColorBlendAttachmentState blendAlpha() {
  VkPipelineColorBlendAttachmentState b = blendNone();
  b.blendEnable = VK_TRUE;
  b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  b.colorBlendOp = VK_BLEND_OP_ADD;
  b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  b.alphaBlendOp = VK_BLEND_OP_ADD;
  return b;
}

VkPipelineColorBlendAttachmentState blendAdd() {
  VkPipelineColorBlendAttachmentState b = blendNone();
  b.blendEnable = VK_TRUE;
  b.srcColorBlendFactor = b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
  b.colorBlendOp = VK_BLEND_OP_ADD;
  b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  b.alphaBlendOp = VK_BLEND_OP_ADD;
  return b;
}

VkPipelineColorBlendAttachmentState blendMultiply() {
  VkPipelineColorBlendAttachmentState b = blendNone();
  b.blendEnable = VK_TRUE;
  b.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
  b.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
  b.colorBlendOp = VK_BLEND_OP_ADD;
  b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  b.alphaBlendOp = VK_BLEND_OP_ADD;
  return b;
}

bool createImage(Ctx& ctx, const ImageDesc& d, Image& out) {
  VkImageCreateInfo ii{};
  ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = d.format;
  ii.extent = {d.extent.width, d.extent.height, 1};
  ii.mipLevels = d.mips;
  ii.arrayLayers = d.layers;
  ii.samples = d.samples;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = d.usage;
  ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VmaAllocationCreateInfo ai{};
  ai.usage = VMA_MEMORY_USAGE_AUTO;
  ai.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  if (vmaCreateImage(ctx.vma, &ii, &ai, &out.image, &out.alloc, nullptr) != VK_SUCCESS) return false;
  out.view = createImageView(ctx, out.image, d.format, d.aspect, 0, d.mips, 0, d.layers,
                             d.layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
  return out.view != VK_NULL_HANDLE;
}

VkImageView createImageView(Ctx& ctx, VkImage image, VkFormat format, VkImageAspectFlags aspect, uint32_t mip,
                            uint32_t mipCount, uint32_t layer, uint32_t layerCount, VkImageViewType type) {
  VkImageViewCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vi.image = image;
  vi.viewType = type;
  vi.format = format;
  vi.subresourceRange = {aspect, mip, mipCount, layer, layerCount};
  VkImageView view = VK_NULL_HANDLE;
  if (vkCreateImageView(ctx.dev, &vi, nullptr, &view) != VK_SUCCESS) return VK_NULL_HANDLE;
  return view;
}

void destroyImage(Ctx& ctx, Image& im) {
  if (im.view) vkDestroyImageView(ctx.dev, im.view, nullptr);
  if (im.image) vmaDestroyImage(ctx.vma, im.image, im.alloc);
  im = {};
}

VkAttachmentDescription attachment(VkFormat fmt, VkAttachmentLoadOp load, VkAttachmentStoreOp store,
                                   VkImageLayout finalLayout, VkSampleCountFlagBits samples) {
  VkAttachmentDescription a{};
  a.format = fmt;
  a.samples = samples;
  a.loadOp = load;
  a.storeOp = store;
  a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  a.finalLayout = finalLayout;
  return a;
}

VkRenderPass makeRenderPass(Ctx& ctx, const std::vector<VkAttachmentDescription>& atts, uint32_t colors, bool depth,
                            const std::vector<VkSubpassDependency>& deps) {
  std::vector<VkAttachmentReference> colRefs;
  for (uint32_t i = 0; i < colors; ++i) colRefs.push_back({i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
  const VkAttachmentReference depRef{colors, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = colors;
  sub.pColorAttachments = colRefs.data();
  sub.pDepthStencilAttachment = depth ? &depRef : nullptr;
  VkRenderPassCreateInfo rp{};
  rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rp.attachmentCount = static_cast<uint32_t>(atts.size());
  rp.pAttachments = atts.data();
  rp.subpassCount = 1;
  rp.pSubpasses = &sub;
  rp.dependencyCount = static_cast<uint32_t>(deps.size());
  rp.pDependencies = deps.data();
  VkRenderPass pass = VK_NULL_HANDLE;
  if (vkCreateRenderPass(ctx.dev, &rp, nullptr, &pass) != VK_SUCCESS) log(LogLevel::Error, "vkCreateRenderPass failed");
  return pass;
}

VkFramebuffer makeFramebuffer(Ctx& ctx, VkRenderPass pass, const std::vector<VkImageView>& views, VkExtent2D size) {
  VkFramebufferCreateInfo fbi{};
  fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fbi.renderPass = pass;
  fbi.attachmentCount = static_cast<uint32_t>(views.size());
  fbi.pAttachments = views.data();
  fbi.width = size.width;
  fbi.height = size.height;
  fbi.layers = 1;
  VkFramebuffer fb = VK_NULL_HANDLE;
  if (vkCreateFramebuffer(ctx.dev, &fbi, nullptr, &fb) != VK_SUCCESS) log(LogLevel::Error, "vkCreateFramebuffer failed");
  return fb;
}

VkPipeline makeComputePipeline(Ctx& ctx, VkShaderModule module, VkPipelineLayout layout) {
  VkComputePipelineCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  ci.stage.module = module;
  ci.stage.pName = "main";
  ci.layout = layout;
  VkPipeline pipe = VK_NULL_HANDLE;
  if (vkCreateComputePipelines(ctx.dev, VK_NULL_HANDLE, 1, &ci, nullptr, &pipe) != VK_SUCCESS) {
    log(LogLevel::Error, "vkCreateComputePipelines failed");
    return VK_NULL_HANDLE;
  }
  return pipe;
}

VkPipeline makeComputePipeline(Ctx& ctx, const uint32_t* code, size_t bytes, VkPipelineLayout layout) {
  VkShaderModule m = createShaderModule(ctx, code, bytes);
  VkPipeline pipe = makeComputePipeline(ctx, m, layout);
  vkDestroyShaderModule(ctx.dev, m, nullptr);
  return pipe;
}

VkDescriptorSetLayout makeSetLayout(Ctx& ctx, const std::vector<std::pair<uint32_t, VkDescriptorType>>& bindings,
                                    VkShaderStageFlags stages) {
  std::vector<VkDescriptorSetLayoutBinding> b;
  b.reserve(bindings.size());
  for (const auto& [binding, type] : bindings) b.push_back({binding, type, 1, stages, nullptr});
  VkDescriptorSetLayoutCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  ci.bindingCount = static_cast<uint32_t>(b.size());
  ci.pBindings = b.data();
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  if (vkCreateDescriptorSetLayout(ctx.dev, &ci, nullptr, &layout) != VK_SUCCESS)
    log(LogLevel::Error, "vkCreateDescriptorSetLayout failed");
  return layout;
}

VkDescriptorSet allocSet(Ctx& ctx, VkDescriptorSetLayout layout, VkDescriptorPool pool) {
  VkDescriptorSetAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  ai.descriptorPool = pool ? pool : ctx.descPool;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &layout;
  VkDescriptorSet set = VK_NULL_HANDLE;
  if (vkAllocateDescriptorSets(ctx.dev, &ai, &set) != VK_SUCCESS) log(LogLevel::Error, "vkAllocateDescriptorSets failed");
  return set;
}

void writeBuffer(Ctx& ctx, VkDescriptorSet set, uint32_t binding, VkDescriptorType type, VkBuffer buffer,
                 VkDeviceSize size) {
  const VkDescriptorBufferInfo info{buffer, 0, size};
  VkWriteDescriptorSet w{};
  w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  w.dstSet = set;
  w.dstBinding = binding;
  w.descriptorCount = 1;
  w.descriptorType = type;
  w.pBufferInfo = &info;
  vkUpdateDescriptorSets(ctx.dev, 1, &w, 0, nullptr);
}

void writeImage(Ctx& ctx, VkDescriptorSet set, uint32_t binding, VkDescriptorType type, VkImageView view,
                VkImageLayout layout, VkSampler sampler) {
  const VkDescriptorImageInfo info{sampler, view, layout};
  VkWriteDescriptorSet w{};
  w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  w.dstSet = set;
  w.dstBinding = binding;
  w.descriptorCount = 1;
  w.descriptorType = type;
  w.pImageInfo = &info;
  vkUpdateDescriptorSets(ctx.dev, 1, &w, 0, nullptr);
}

void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkAccessFlags srcAccess, VkPipelineStageFlags dst,
                   VkAccessFlags dstAccess) {
  VkMemoryBarrier b{};
  b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  b.srcAccessMask = srcAccess;
  b.dstAccessMask = dstAccess;
  vkCmdPipelineBarrier(cmd, src, dst, 0, 1, &b, 0, nullptr, 0, nullptr);
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                  VkImageLayout to, VkPipelineStageFlags src, VkAccessFlags srcAccess, VkPipelineStageFlags dst,
                  VkAccessFlags dstAccess) {
  VkImageMemoryBarrier b{};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  b.srcAccessMask = srcAccess;
  b.dstAccessMask = dstAccess;
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {aspect, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
}

}  // namespace bvk
