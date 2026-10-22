// BlueVK's tests: the feature chain and a headless context (no window) on
// whatever GPU there is: buffers filled and read back, images, descriptors,
// a render pass and its framebuffer. No GPU: skipped (77).
#include <bluevk/bluevk.hpp>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

void testFeatureChain() {
  bvk::Features f;
  auto tail = bvk::vkStruct<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT);
  VkPhysicalDeviceFeatures2* head = f.link(VK_API_VERSION_1_3, &tail);
  expect(head == &f.core && f.core.pNext == &f.v11 && f.v11.pNext == &f.v12 && f.v12.pNext == &f.v13 &&
             f.v13.pNext == &tail,
         "1.3's chain: core, 1.1, 1.2, 1.3, then the tail");
  f.link(VK_API_VERSION_1_2);
  expect(f.v12.pNext == nullptr && f.core.pNext == &f.v11, "1.2's chain stops at 1.2");
  f.link(VK_API_VERSION_1_0);
  expect(f.core.pNext == nullptr, "1.0's chain: the core features alone");
  f.link(VK_API_VERSION_1_3);
  f.unlink();
  expect(!f.core.pNext && !f.v11.pNext && !f.v12.pNext && !f.v13.pNext, "unlinked: no pointers left");

  bvk::DeviceChoice choice;
  auto& af = choice.add<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT);
  expect(af.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT &&
             !af.shaderBufferFloat32AtomicAdd && choice.chain.size() == 1,
         "an extension's struct added: its sType set, the rest zero");
}

void testContext(bvk::Ctx& ctx) {
  expect(ctx.dev && ctx.queue && ctx.vma && ctx.cmd && ctx.fence && !ctx.surface && !ctx.swapchain,
         "headless: a device, no surface or swapchain");
  expect(!ctx.gpu.name.empty() && !ctx.gpu.api.empty(), "the GPU described");
  expect(ctx.hasExtension("VK_none_such") == false, "an extension not asked for isn't enabled");
  expect(ctx.enabled.v13.shaderDemoteToHelperInvocation == VK_TRUE, "the picker's features enabled");

  // A buffer filled on the GPU, copied into a readback buffer.
  bvk::Buffer dev, back;
  bvk::createDeviceBuffer(ctx, dev, 256, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  bvk::createReadbackBuffer(ctx, back, 256);
  expect(dev.b && !dev.mapped && back.b && back.mapped, "device and readback buffers");
  bvk::executeImmediate(ctx, [&](VkCommandBuffer cmd) {
    vkCmdFillBuffer(cmd, dev.b, 0, 256, 0xB1E5B1E5u);
    bvk::memoryBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    const VkBufferCopy copy{0, 0, 256};
    vkCmdCopyBuffer(cmd, dev.b, back.b, 1, &copy);
  });
  bool same = true;
  for (int i = 0; i < 64; ++i) same &= static_cast<const uint32_t*>(back.mapped)[i] == 0xB1E5B1E5u;
  expect(same, "filled on the GPU, read back");

  bvk::Buffer up;
  bvk::createBuffer(ctx, up, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  expect(up.b && up.mapped && up.size == 64, "an upload buffer, mapped");

  // An image and a pass drawing into it.
  bvk::Image im;
  bvk::ImageDesc d;
  d.extent = {64, 32};
  d.format = VK_FORMAT_R8G8B8A8_UNORM;
  d.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  expect(bvk::createImage(ctx, d, im) && im.view, "an image and its view");
  const VkRenderPass pass = bvk::makeRenderPass(
      ctx,
      {bvk::attachment(d.format, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)},
      1, false, {});
  const VkFramebuffer fb = bvk::makeFramebuffer(ctx, pass, {im.view}, d.extent);
  expect(pass && fb, "a render pass and its framebuffer");
  bvk::executeImmediate(ctx, [&](VkCommandBuffer cmd) {
    VkClearValue clear{};
    clear.color = {{0.2f, 0.4f, 0.8f, 1.0f}};
    auto rbi = bvk::vkStruct<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    rbi.renderPass = pass;
    rbi.framebuffer = fb;
    rbi.renderArea.extent = d.extent;
    rbi.clearValueCount = 1;
    rbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cmd);
  });

  // Descriptors from the shared pool.
  const VkDescriptorSetLayout layout = bvk::makeSetLayout(
      ctx, {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}}, VK_SHADER_STAGE_ALL);
  const VkDescriptorSet set = bvk::allocSet(ctx, layout);
  expect(ctx.descPool && layout && set, "a set from the shared pool");
  bvk::writeBuffer(ctx, set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, up.b);
  bvk::writeBuffer(ctx, set, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, dev.b);
  expect(vkFreeDescriptorSets(ctx.dev, ctx.descPool, 1, &set) == VK_SUCCESS, "the set freed back");

  expect(bvk::groupsFor(257) == 2 && bvk::groupsFor(256) == 1 && bvk::groupsFor(0) == 0, "groupsFor rounds up");

  vkDestroyDescriptorSetLayout(ctx.dev, layout, nullptr);
  vkDestroyFramebuffer(ctx.dev, fb, nullptr);
  vkDestroyRenderPass(ctx.dev, pass, nullptr);
  bvk::destroyImage(ctx, im);
  bvk::destroyBuffer(ctx, up);
  bvk::destroyBuffer(ctx, back);
  bvk::destroyBuffer(ctx, dev);
  expect(!dev.b && !im.image, "destroyed: handles cleared");
}

}  // namespace

int main() {
  testFeatureChain();

  int errors = 0;
  bvk::setLog([&](bvk::LogLevel level, std::string_view msg) {
    if (level == bvk::LogLevel::Error) ++errors;
    if (level >= bvk::LogLevel::Warn) std::printf("      [bluevk] %.*s\n", int(msg.size()), msg.data());
  });

  bvk::ContextDesc desc;
  desc.appName = "BlueVK tests";
  desc.descriptorPool = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}};
  desc.descriptorSets = 4;
  bool picked = false;
  desc.pick = [&](const bvk::DeviceCandidate& cand, bvk::DeviceChoice& choice, std::string& why) {
    if (!cand.supported.v13.shaderDemoteToHelperInvocation) {
      why = "no shaderDemoteToHelperInvocation";
      return false;
    }
    choice.enable.v13.shaderDemoteToHelperInvocation = VK_TRUE;
    picked = true;
    return true;
  };
  bvk::Ctx ctx;
  if (!bvk::createContext(ctx, nullptr, desc)) {
    bvk::destroyContext(ctx);
    std::printf("skip  no Vulkan 1.3 GPU\n");
    return failures ? 1 : 77;
  }
  expect(picked, "the picker asked");
  testContext(ctx);
  bvk::destroyContext(ctx);
  expect(!ctx.dev && !ctx.inst, "the context destroyed: back to its defaults");
  expect(errors == 0, "nothing logged as an error (validation included, in debug builds)");

  std::printf("%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
