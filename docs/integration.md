# Integration

## Adding BlueVK to a CMake project

Add it once the game's Vulkan and SDL3 targets exist, so BlueVK links the same ones:

```cmake
set(BLUEVK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../BlueVK" CACHE PATH "BlueVK's source")
set(BLUEVK_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(BLUEVK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BLUEVK_SDL_TARGET PkgConfig::SDL3 CACHE STRING "" FORCE)  # the game's SDL3 target
add_subdirectory("${BLUEVK_DIR}" bluevk)
target_link_libraries(my_game PUBLIC bluevk)
```

- **Vulkan:** if a `Vulkan::Vulkan` target already exists (the game's own, for example Hexoid's on Windows), BlueVK uses it. Otherwise it calls `find_package(Vulkan)`, and fetches the Vulkan headers if there's no SDK.
- **SDL3:** `BLUEVK_SDL_TARGET` names the game's target (`PkgConfig::SDL3`, `SDL3::SDL3-static`, ...). If it's left empty, BlueVK finds SDL3 itself.
- **VMA:** BlueVK compiles VMA's implementation. Games include `<bluevk/vma.hpp>` (it comes with `<bluevk/bluevk.hpp>`) and must not compile `VMA_IMPLEMENTATION` a second time.

BlueVK is a static library, and its demo and tests are only built when it's the top-level project.

## Deriving the game's context

The game's context derives from `bvk::Ctx` and adds its own render passes and targets. Keeping its existing function names as thin wrappers means the rest of the game doesn't change:

```cpp
namespace game {
using namespace bvk;  // Buffer, Image, makeGraphicsPipeline... unqualified

struct Ctx : bvk::Ctx {
  VkExtent2D extent{};  // what's rendered
  Image sceneColor, sceneDepth;
  VkRenderPass scenePass = VK_NULL_HANDLE, presentPass = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> presentFbs;
};

bool createCtx(Ctx& c, SDL_Window* win) {
  ContextDesc desc;
  desc.appName = "Game";
  desc.pick = pickDevice;
  if (!createContext(c, win, desc)) return false;
  c.extent = c.swapExtent;
  createRenderPasses(c);
  return createRenderTargets(c);
}

bool ctxEnsureSwapchain(Ctx& c, SDL_Window* win, bool force) {
  switch (ensureSwapchain(c, win, force)) {
    case Swapchain::Unchanged: return true;
    case Swapchain::Failed: return false;
    case Swapchain::Rebuilt: break;
  }
  c.extent = c.swapExtent;
  return createRenderTargets(c);  // its framebuffers over the new colorViews
}

void destroyCtx(Ctx& c) {
  if (c.dev) { vkDeviceWaitIdle(c.dev); destroyRenderTargets(c); /* passes */ }
  destroyContext(c);
  c = {};
}
}  // namespace game
```

FloodSim's and Hexoid's `src/vkctx.cpp` are both written this way.

## Picking the GPU and its features

`ContextDesc::pick` is called once for each GPU that has Vulkan `apiVersion` and the queue the game needs (graphics, plus compute unless `computeQueue = false`, plus presenting to the window when there is one). It sees what the GPU supports and fills in what to enable:

- `cand.supported`: core features for 1.0 to 1.3 (`core.features`, `v11`, `v12`, `v13`).
- `cand.hasExtension(name)`, `cand.features<T>(sType)` and `cand.properties<T>(sType)` query a GPU's extensions and their structs.
- `choice.enable`: the core features to turn on (all off to begin with).
- `choice.extensions`: device extensions. The swapchain's is added automatically when there's a window.
- `choice.add<T>(sType)`: an extension's feature struct, chained in after the core ones.
- `choice.score`: added to the ranking, which is discrete GPUs first, then the most VRAM.

Return false with `why` set to pass a GPU over. The reason is logged, and if every GPU is passed over, the final error lists all the reasons.

```cpp
// A required extension (FloodSim's float atomics):
if (!cand.hasExtension(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME)) { why = "no float atomics"; return false; }
auto& af = choice.add<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>(
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT);
af.shaderBufferFloat32AtomicAdd = VK_TRUE;
choice.extensions.push_back(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);

// An optional feature (Hexoid's wireframes): enabled where the GPU has it.
choice.enable.core.features.fillModeNonSolid = cand.supported.core.features.fillModeNonSolid;
```

After `createContext`, `ctx.enabled` and `ctx.hasExtension(name)` say what was turned on. Read your optional features from those, as Hexoid does for ray queries.

Don't add an extension struct whose features are already in `VkPhysicalDeviceVulkan11/12/13Features`, such as `VkPhysicalDeviceBufferDeviceAddressFeatures`. Set the field in `choice.enable.v12` instead, since Vulkan doesn't allow both. Enabling `v12.bufferDeviceAddress` also turns on VMA's buffer-device-address flag.

## Logging

By default BlueVK's messages, and the validation layer's, go to stderr. To send them to the game's own log:

```cpp
bvk::setLog([](bvk::LogLevel level, std::string_view msg) {
  switch (level) {
    case bvk::LogLevel::Error: LOG_VK_ERROR("{}", msg); break;
    case bvk::LogLevel::Warn:  LOG_VK_WARN("{}", msg); break;
    case bvk::LogLevel::Info:  LOG_VK_INFO("{}", msg); break;
    default:                   LOG_VK_TRACE("{}", msg); break;
  }
});
```

The validation layer is on in builds without `NDEBUG`, when it's installed (`ContextDesc::validation`). Whether it's active is recorded in `ctx.gpu.validation`.
