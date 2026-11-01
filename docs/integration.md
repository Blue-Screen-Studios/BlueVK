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
  Config config;
  config.appName = "Game";
  config.buffering = Buffering::Double;
  config.pick = pickDevice;
  if (createContext(c, win, config) != ContextResult::Ok) return false;
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

## Configuring the context

Everything the context should be goes in one `bvk::Config`:

| Field | Default | |
|---|---|---|
| `appName` | `"BlueVK App"` | The application name given to Vulkan. The engine name is always `"IBX_BlueVK"` (`bvk::kEngineName`). |
| `appVersion` | 0.1.0 | |
| `apiVersion` | 1.3 | GPUs below it are passed over. |
| `buffering` | `Buffering::Single` | `Single`, `Double` or `Triple`: the frames in flight (`ctx.framesInFlight`). The swapchain gets at least that many images. |
| `gpu` | `GpuTarget::PreferDiscrete` | `Any` (any GPU, ranked by VRAM), `RequireDiscrete` (only discrete GPUs) or `PreferDiscrete` (discrete ahead of the rest). Software renderers always rank last. |
| `computeQueue` | true | The one queue does compute too. |
| `validation` | on without `NDEBUG` | `VK_LAYER_KHRONOS_validation`, when installed. |
| `requiredFeatures` | none | Any feature set here (`core.features`, `v11`, `v12`, `v13`) must be supported, and is enabled. |
| `requiredExtensions` | none | Device extensions that must be supported, and are enabled. |
| `pick` | none | The game's own look at each GPU that meets the rest (below). |
| `descriptorPool`, `descriptorSets` | none | A shared descriptor pool whose sets can be freed. |

`createContext(ctx, window, config, &detail)` returns `ContextResult::Ok`, or why the config couldn't be met. On failure the reason is logged, `detail` (optional) gets the particulars, such as which GPU lacked which feature, and the context is destroyed so it can be tried again with a different config. `toString(result)` gives a short description.

| `ContextResult` | |
|---|---|
| `InvalidConfig` | The config itself is wrong: required features above its `apiVersion`. |
| `VulkanUnavailable` | No Vulkan driver or loader, or none for the window system. |
| `InstanceFailed` | `vkCreateInstance` failed for another reason. |
| `SurfaceFailed` | The window's surface couldn't be made. |
| `NoGpu` | No GPU at all. |
| `NoDiscreteGpu` | `RequireDiscrete`, and no GPU is discrete. |
| `ApiVersionUnsupported` | No GPU has `apiVersion`. |
| `NoSuitableQueue` | No queue for graphics (plus compute, plus presenting). |
| `CannotPresent` | No GPU can present to the window. |
| `MissingExtension` | A required extension is missing. |
| `MissingFeature` | A required feature is missing. |
| `BufferingUnsupported` | The window's swapchain can't hold as many images as the buffering. |
| `RejectedByPicker` | `pick` turned down every GPU left. |
| `DeviceFailed` | `vkCreateDevice` failed. |
| `ResourceFailed` | The allocator, command pool and buffers, descriptor pool or sync objects couldn't be made. |
| `SwapchainFailed` | The first swapchain couldn't be made. |

GPUs are checked in the order of that list, from `NoDiscreteGpu` down. When several GPUs fail for different reasons, the result is the reason of the one that got furthest. For example, if one GPU isn't discrete and another is discrete but lacks a required feature, the result is `MissingFeature`.

## Picking the GPU and its features

For anything the config can't express (optional features, an extension's own feature struct, properties such as subgroup operations, a score), use `Config::pick`. It is called once for each GPU that meets everything else in the config. It sees what the GPU supports and fills in what to enable:

- `cand.supported`: core features for 1.0 to 1.3 (`core.features`, `v11`, `v12`, `v13`).
- `cand.hasExtension(name)`, `cand.features<T>(sType)` and `cand.properties<T>(sType)` query a GPU's extensions and their structs.
- `choice.enable`: the core features to turn on (all off to begin with).
- `choice.extensions`: device extensions. The swapchain's is added automatically when there's a window.
- `choice.add<T>(sType)`: an extension's feature struct, chained in after the core ones.
- `choice.score`: added to the ranking, which is discrete GPUs first (unless `GpuTarget::Any`), then the most VRAM.

The config's required features and extensions are enabled on top of whatever the pick chooses.

Return false with `why` set to pass a GPU over. The reason is logged, and if every GPU is passed over, the final error lists all the reasons.

```cpp
// A required extension's feature struct (FloodSim's float atomics; the
// extension itself in config.requiredExtensions):
auto atomics = cand.features<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>(
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT);
if (!atomics.shaderBufferFloat32AtomicAdd) { why = "no float atomic adds"; return false; }
choice.add<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>(
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT).shaderBufferFloat32AtomicAdd = VK_TRUE;

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

The validation layer is on in builds without `NDEBUG`, when it's installed (`Config::validation`). Whether it's active is recorded in `ctx.gpu.validation`.
