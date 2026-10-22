// BlueVK's demo: a window, its swapchain cleared to a slowly turning color
// and presented each frame, rebuilt as the window is resized. The smallest
// game loop on BlueVK: a derived context with its own present pass.
#include <SDL3/SDL.h>

#include <bluevk/bluevk.hpp>

#include <cmath>
#include <cstdio>

namespace {

// The demo's context: BlueVK's, plus its one pass over the swapchain image.
struct DemoCtx : bvk::Ctx {
  VkRenderPass presentPass = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> presentFbs;
};

void destroyFramebuffers(DemoCtx& c) {
  for (VkFramebuffer fb : c.presentFbs) vkDestroyFramebuffer(c.dev, fb, nullptr);
  c.presentFbs.clear();
}

void createFramebuffers(DemoCtx& c) {
  destroyFramebuffers(c);
  for (VkImageView v : c.colorViews) c.presentFbs.push_back(bvk::makeFramebuffer(c, c.presentPass, {v}, c.swapExtent));
}

// One frame: acquire, clear, present. False: the swapchain is out of date.
bool drawFrame(DemoCtx& c, float t) {
  vkWaitForFences(c.dev, 1, &c.fence, VK_TRUE, UINT64_MAX);
  VkResult r = vkAcquireNextImageKHR(c.dev, c.swapchain, UINT64_MAX, c.acquire, VK_NULL_HANDLE, &c.image);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) return false;
  vkResetFences(c.dev, 1, &c.fence);

  vkResetCommandBuffer(c.cmd, 0);
  auto bi = bvk::vkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(c.cmd, &bi);
  VkClearValue clear{};
  clear.color = {{0.1f + 0.1f * std::sin(t), 0.25f + 0.1f * std::sin(t * 0.7f + 2.0f), 0.5f, 1.0f}};
  auto rbi = bvk::vkStruct<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
  rbi.renderPass = c.presentPass;
  rbi.framebuffer = c.presentFbs[c.image];
  rbi.renderArea.extent = c.swapExtent;
  rbi.clearValueCount = 1;
  rbi.pClearValues = &clear;
  vkCmdBeginRenderPass(c.cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdEndRenderPass(c.cmd);
  vkEndCommandBuffer(c.cmd);

  const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  auto si = bvk::vkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &c.acquire;
  si.pWaitDstStageMask = &wait;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &c.cmd;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &c.present[c.image];
  vkQueueSubmit(c.queue, 1, &si, c.fence);

  auto pi = bvk::vkStruct<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &c.present[c.image];
  pi.swapchainCount = 1;
  pi.pSwapchains = &c.swapchain;
  pi.pImageIndices = &c.image;
  r = vkQueuePresentKHR(c.queue, &pi);
  return r != VK_ERROR_OUT_OF_DATE_KHR && r != VK_SUBOPTIMAL_KHR;
}

}  // namespace

int main() {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* win = SDL_CreateWindow("BlueVK demo", 960, 540, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  DemoCtx c;
  bvk::ContextDesc desc;
  desc.appName = "BlueVK demo";
  desc.computeQueue = false;
  if (!win || !bvk::createContext(c, win, desc)) return 1;

  c.presentPass = bvk::makeRenderPass(
      c,
      {bvk::attachment(c.colorFmt, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)},
      1, false,
      {{VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0}});
  createFramebuffers(c);
  std::printf("BlueVK demo on %s (%s, Vulkan %s). Close the window to quit.\n", c.gpu.name.c_str(),
              c.gpu.type.c_str(), c.gpu.api.c_str());

  bool running = true, outOfDate = false;
  const uint64_t start = SDL_GetTicks();
  while (running) {
    for (SDL_Event e; SDL_PollEvent(&e);)
      if (e.type == SDL_EVENT_QUIT) running = false;
    const bvk::Swapchain s = bvk::ensureSwapchain(c, win, outOfDate);
    if (s == bvk::Swapchain::Failed) break;
    if (s == bvk::Swapchain::Rebuilt) createFramebuffers(c);
    if (SDL_GetWindowFlags(win) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(16);
      continue;
    }
    outOfDate = !drawFrame(c, float(SDL_GetTicks() - start) * 0.001f);
  }

  vkDeviceWaitIdle(c.dev);
  destroyFramebuffers(c);
  vkDestroyRenderPass(c.dev, c.presentPass, nullptr);
  bvk::destroyContext(c);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
