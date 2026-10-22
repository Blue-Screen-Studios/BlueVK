# BlueVK

**The Vulkan groundwork a game's renderer sits on, in C++20.**

BlueVK makes the instance, picks the GPU, creates the device, its queue and memory allocator, builds the window's swapchain and keeps one frame in flight. It also has the small helpers every renderer ends up writing: pipelines from a short description, images with their views, render passes, framebuffers, descriptors and barriers.

BlueVK draws nothing and holds no render targets. The game derives its own context from `bvk::Ctx` and adds its passes and images to it, so its renderer reads `ctx.dev`, `ctx.swapchain` and `ctx.sceneColor` from the same struct.

| BlueVK does | The game does |
|---|---|
| Instance, validation layer (debug builds) and debug messenger | Says which GPU features and extensions it needs, in a picker callback |
| Picks the GPU: discrete first, then the most VRAM | Creates its render passes, targets and pipelines |
| Device, queue, VMA allocator, optional shared descriptor pool | Records and submits its frames |
| Swapchain, rebuilt when the window resizes | Rebuilds its framebuffers when the swapchain is rebuilt |
| Command buffer, acquire/present semaphores, frame fence | Routes BlueVK's log messages into its own logger (optional) |

Used by FloodSim and Hexoid.

## Quick start

```cpp
#include <bluevk/bluevk.hpp>

struct GameCtx : bvk::Ctx {  // BlueVK's context plus the game's own
  VkRenderPass presentPass = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> presentFbs;
};

bvk::ContextDesc desc;
desc.appName = "My Game";
desc.pick = [](const bvk::DeviceCandidate& gpu, bvk::DeviceChoice& use, std::string& why) {
  if (!gpu.supported.core.features.independentBlend) {
    why = "no independent blending";  // logged; this GPU is passed over
    return false;
  }
  use.enable.core.features.independentBlend = VK_TRUE;
  use.enable.v13.shaderDemoteToHelperInvocation = VK_TRUE;
  return true;
};

GameCtx ctx;
if (!bvk::createContext(ctx, window, desc)) return false;  // window: SDL_WINDOW_VULKAN
// ... make presentPass and presentFbs from ctx.colorViews ...

// Every frame:
switch (bvk::ensureSwapchain(ctx, window)) {
  case bvk::Swapchain::Rebuilt: rebuildFramebuffers(ctx); break;
  case bvk::Swapchain::Failed: return false;
  case bvk::Swapchain::Unchanged: break;
}
// wait ctx.fence, acquire into ctx.image (signals ctx.acquire), record ctx.cmd,
// submit (signals ctx.present[ctx.image]), present.

// At the end: the game's own objects first, then
bvk::destroyContext(ctx);
```

`demo/main.cpp` is a complete, runnable version of this.

## Building

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build      # skipped (not failed) without a Vulkan 1.3 GPU
./build/bluevk_demo         # a window cleared to a slowly changing colour
```

It needs a C++20 compiler, the Vulkan loader and SDL3. Without a Vulkan SDK, BlueVK fetches the Vulkan headers itself. Vulkan Memory Allocator 3.4.0 is included in `third_party/`.

## Documentation

- [Getting started](docs/getting-started.md): a context, a frame, resizing, cleaning up.
- [Integration](docs/integration.md): adding BlueVK to a game's CMake, deriving its context, picking features, logging.
- [API reference](docs/api-reference.md): every type and function.

## Licence

MIT (see `LICENSE.md`). Vulkan Memory Allocator is AMD's, under MIT; its notice is at the top of `third_party/vk_mem_alloc.h`.
