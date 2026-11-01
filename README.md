# BlueVK

**The Vulkan groundwork a game's renderer sits on, in C++20.**

BlueVK creates the Vulkan instance, picks the GPU that meets the game's config, and creates the device, its queue and a memory allocator. It builds the window's swapchain and keeps one, two or three frames in flight. It also has the small helpers every renderer ends up writing: pipelines from a short description, images with their views, render passes, framebuffers, descriptors and barriers.

BlueVK draws nothing and holds no render targets. The game derives its own context from `bvk::Ctx` and adds its passes and images to it, so its renderer reads `ctx.dev`, `ctx.swapchain` and its own `ctx.sceneColor` from the same struct.

| BlueVK does | The game does |
|---|---|
| Instance (engine name `IBX_BlueVK`), validation layer in debug builds, debug messenger | Fills in a `bvk::Config`: its name, buffering, GPU target, required features and extensions |
| Picks the best GPU that meets the config, or returns the reason none does (`bvk::ContextResult`) | Shows that reason to the player if creation fails |
| Device, queue, VMA allocator, optional shared descriptor pool | Creates its render passes, targets and pipelines |
| Swapchain, rebuilt when the window resizes | Rebuilds its framebuffers when the swapchain is rebuilt |
| Per frame in flight: command buffer, acquire semaphore, fence; a present semaphore per swapchain image | Records, submits and presents its frames, and calls `advanceFrame` |
| Logs through a callback | Routes BlueVK's log into its own logger (optional) |

Used by FloodSim and Hexoid.

## Getting started

This is everything a game adds to run on BlueVK: a window cleared to a colour and presented each frame, surviving resizes. `demo/main.cpp` is the same code as a complete program.

### 1. CMake

Add BlueVK after the game's own Vulkan and SDL3 targets exist, so it links the same ones:

```cmake
set(BLUEVK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../BlueVK" CACHE PATH "BlueVK's source")
set(BLUEVK_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(BLUEVK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BLUEVK_SDL_TARGET SDL3::SDL3 CACHE STRING "" FORCE)  # the game's SDL3 target (empty: BlueVK finds SDL3)
add_subdirectory("${BLUEVK_DIR}" bluevk)
target_link_libraries(my_game PRIVATE bluevk)
```

### 2. The game's context

Derive from `bvk::Ctx` and add what the game owns. Here that's one render pass over the swapchain image, and a framebuffer per swapchain image:

```cpp
#include <SDL3/SDL.h>
#include <bluevk/bluevk.hpp>

struct GameCtx : bvk::Ctx {
  VkRenderPass presentPass = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> presentFbs;  // one per swapchain image (ctx.colorViews)
};

void destroyFramebuffers(GameCtx& c) {
  for (VkFramebuffer fb : c.presentFbs) vkDestroyFramebuffer(c.dev, fb, nullptr);
  c.presentFbs.clear();
}

void createFramebuffers(GameCtx& c) {
  destroyFramebuffers(c);
  for (VkImageView v : c.colorViews) c.presentFbs.push_back(bvk::makeFramebuffer(c, c.presentPass, {v}, c.swapExtent));
}
```

### 3. Creating it

Make the window with `SDL_WINDOW_VULKAN`, fill in a `bvk::Config`, and create the context. If the config can't be met, `createContext` returns the reason, logs it, and leaves the context destroyed:

```cpp
SDL_Init(SDL_INIT_VIDEO);
SDL_Window* window = SDL_CreateWindow("My Game", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

bvk::Config config;
config.appName = "My Game";
config.buffering = bvk::Buffering::Double;    // Single, Double or Triple frames in flight
config.gpu = bvk::GpuTarget::PreferDiscrete;  // or Any, RequireDiscrete
config.computeQueue = false;                  // graphics only
// What the game can't run without (a GPU lacking any of it is passed over):
config.requiredFeatures.core.features.independentBlend = VK_TRUE;
config.requiredExtensions = {};

GameCtx ctx;
std::string why;
if (const bvk::ContextResult r = bvk::createContext(ctx, window, config, &why); r != bvk::ContextResult::Ok) {
  // e.g. "a required feature is missing" / "Intel UHD 620: no independentBlend"
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, bvk::toString(r), why.c_str(), window);
  return 1;
}

// The game's pass: clear the swapchain image, leave it ready to present.
ctx.presentPass = bvk::makeRenderPass(
    ctx,
    {bvk::attachment(ctx.colorFmt, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                     VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)},
    1, false,
    {{VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0}});
createFramebuffers(ctx);
```

### 4. A frame

BlueVK leaves the frame loop to the game. Each frame uses the current frame in flight's fence, acquire semaphore and command buffer (`ctx.fence()`, `ctx.acquire()`, `ctx.cmd()`), then moves on with `advanceFrame`:

```cpp
// False: the swapchain is out of date (rebuild it before the next frame).
bool drawFrame(GameCtx& c) {
  const VkFence fence = c.fence();
  const VkCommandBuffer cmd = c.cmd();
  const VkSemaphore acquire = c.acquire();
  // Wait until this frame in flight's last use is done on the GPU.
  vkWaitForFences(c.dev, 1, &fence, VK_TRUE, UINT64_MAX);
  VkResult r = vkAcquireNextImageKHR(c.dev, c.swapchain, UINT64_MAX, acquire, VK_NULL_HANDLE, &c.image);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) return false;
  vkResetFences(c.dev, 1, &fence);

  // Record: the game's passes. Here, one that clears the image.
  vkResetCommandBuffer(cmd, 0);
  auto bi = bvk::vkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  VkClearValue clear{};
  clear.color = {{0.1f, 0.25f, 0.5f, 1.0f}};
  auto rbi = bvk::vkStruct<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
  rbi.renderPass = c.presentPass;
  rbi.framebuffer = c.presentFbs[c.image];
  rbi.renderArea.extent = c.swapExtent;
  rbi.clearValueCount = 1;
  rbi.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  // ... the game's draws ...
  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);

  // Submit: wait for the image, signal it's ready to present, signal the fence.
  const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  auto si = bvk::vkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &acquire;
  si.pWaitDstStageMask = &wait;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &c.present[c.image];
  vkQueueSubmit(c.queue, 1, &si, fence);

  // Present, then on to the next frame in flight.
  auto pi = bvk::vkStruct<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &c.present[c.image];
  pi.swapchainCount = 1;
  pi.pSwapchains = &c.swapchain;
  pi.pImageIndices = &c.image;
  r = vkQueuePresentKHR(c.queue, &pi);
  bvk::advanceFrame(c);
  return r != VK_ERROR_OUT_OF_DATE_KHR && r != VK_SUBOPTIMAL_KHR;
}
```

With `Double` or `Triple` buffering, the CPU records a frame while the GPU may still be running the one before. Anything the CPU writes every frame (uniform buffers, dynamic vertex buffers, the descriptor sets that point at them) needs one copy per frame in flight, indexed by `ctx.frame`. Anything freed must wait until that frame's fence comes round again. Framebuffers over the swapchain images are indexed by `ctx.image`, as above.

### 5. The loop and resizing

Call `ensureSwapchain` once a frame before drawing. It rebuilds the swapchain when the window's size changes, or when forced after a present reported it out of date. When it's rebuilt, the game rebuilds its framebuffers:

```cpp
bool running = true, outOfDate = false;
while (running) {
  for (SDL_Event e; SDL_PollEvent(&e);)
    if (e.type == SDL_EVENT_QUIT) running = false;
  const bvk::Swapchain s = bvk::ensureSwapchain(ctx, window, outOfDate);
  if (s == bvk::Swapchain::Failed) break;
  if (s == bvk::Swapchain::Rebuilt) createFramebuffers(ctx);
  if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {  // (nothing to draw into)
    SDL_Delay(16);
    continue;
  }
  outOfDate = !drawFrame(ctx);
}
```

Set `ctx.vsync` (FIFO when true, IMMEDIATE where available when false) and call `ensureSwapchain(ctx, window, true)` to apply it.

### 6. Shutting down

Destroy the game's own objects while the device still exists, then the context:

```cpp
vkDeviceWaitIdle(ctx.dev);
destroyFramebuffers(ctx);
vkDestroyRenderPass(ctx.dev, ctx.presentPass, nullptr);
bvk::destroyContext(ctx);  // everything BlueVK made; ctx back to its defaults
SDL_DestroyWindow(window);
SDL_Quit();
```

### Optional: logging

BlueVK's messages, and the validation layer's, go to stderr unless the game routes them:

```cpp
bvk::setLog([](bvk::LogLevel level, std::string_view msg) { myLogger.write(int(level), msg); });
```

## Building BlueVK alone

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build      # skipped (not failed) without a Vulkan 1.3 GPU
./build/bluevk_demo         # a window cleared to a slowly changing colour
```

It needs a C++20 compiler, the Vulkan loader and SDL3. Without a Vulkan SDK, BlueVK fetches the Vulkan headers itself. Vulkan Memory Allocator 3.4.0 is included in `third_party/` and compiled by BlueVK, so the game must not compile `VMA_IMPLEMENTATION` again.

## Documentation

- [Getting started](docs/getting-started.md): a context, a frame, resizing, resources, cleaning up.
- [Integration](docs/integration.md): CMake, deriving the game's context, every `Config` option and `ContextResult` reason, the GPU picker for optional features, logging.
- [API reference](docs/api-reference.md): every type and function.

## Licence

MIT (see `LICENSE.md`). Vulkan Memory Allocator is AMD's, under MIT; its notice is at the top of `third_party/vk_mem_alloc.h`.
