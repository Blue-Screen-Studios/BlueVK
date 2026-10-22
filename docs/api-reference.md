# API reference

Everything is in namespace `bvk`. `<bluevk/bluevk.hpp>` includes both headers.

## `<bluevk/context.hpp>`

### Logging

| | |
|---|---|
| `enum class LogLevel { Trace, Info, Warn, Error }` | |
| `void setLog(LogFn)` | Where messages go: `std::function<void(LogLevel, std::string_view)>`. Empty: stderr. |

### Structs

| | |
|---|---|
| `T vkStruct<T>(VkStructureType)` | A Vulkan struct, zeroed, with its `sType` set. |
| `Features` | `core` (`VkPhysicalDeviceFeatures2`), `v11`, `v12`, `v13`. `link(apiVersion, tail)` chains them and returns the head; `unlink()` clears the pointers. Stored unlinked. |
| `DeviceCandidate` | A GPU being considered: `phys`, `props`, `memory`, `vramBytes`, `supported`, `extensions`, `apiVersion`; `hasExtension(name)`, `features<T>(sType)`, `properties<T>(sType)`. |
| `DeviceChoice` | What to enable: `enable`, `extensions`, `score`; `add<T>(sType)` chains in an extension's feature struct. |
| `DevicePicker` | `bool(const DeviceCandidate&, DeviceChoice&, std::string& why)` |
| `ContextDesc` | `appName`, `appVersion`, `apiVersion` (default 1.3), `computeQueue` (default true), `validation` (default on without `NDEBUG`), `pick`, `descriptorPool` + `descriptorSets` (a shared pool whose sets can be freed; empty: no pool). |
| `GpuInfo` | `name`, `type`, `api`, `driver`, `vramBytes`, `validation`. |

### `Ctx`

| Group | Members |
|---|---|
| Device | `inst`, `debugMessenger`, `surface`, `phys`, `dev`, `qf`, `queue`, `vma`, `descPool`, `apiVersion`, `props`, `timestampPeriod` (ns per tick), `subgroupSize`, `gpu`, `enabled`, `extensions`, `hasExtension(name)` |
| Swapchain settings (read when it's built) | `vsync` (true: FIFO; false: IMMEDIATE where available), `surfaceFormats` (in order of preference; default B8G8R8A8_SRGB / sRGB nonlinear), `swapchainUsage` (default COLOR_ATTACHMENT) |
| Swapchain | `swapchain`, `colorFmt`, `colorSpace`, `swapExtent`, `colorImgs`, `colorViews`, `windowW`, `windowH` |
| Frame | `pool` (resettable buffers), `cmd`, `acquire`, `present[]` (one per swapchain image), `fence` (made signalled), `image` |

### Functions

| | |
|---|---|
| `bool createContext(Ctx&, SDL_Window* win, const ContextDesc&)` | Everything above. `win` null: headless (no surface, no swapchain). |
| `Swapchain ensureSwapchain(Ctx&, SDL_Window*, bool force = false)` | Rebuilds the swapchain if the window's size changed, or if `force`. Returns `Unchanged`, `Rebuilt` (the game rebuilds its framebuffers) or `Failed`. |
| `void destroyContext(Ctx&)` | Waits for the GPU, destroys what `createContext` made and resets the `Ctx`. |
| `struct Buffer { b, alloc, size, mapped }` | |
| `void createBuffer(Ctx&, Buffer&, size, usage, const VmaAllocationCreateInfo* = nullptr)` | Default: host-visible, coherent, persistently mapped, written sequentially. |
| `void createDeviceBuffer(Ctx&, Buffer&, size, usage)` | Device-local. |
| `void createReadbackBuffer(Ctx&, Buffer&, size)` | Host-visible, preferably cached, mapped (a transfer destination). |
| `void destroyBuffer(Ctx&, Buffer&)` | |
| `VkDeviceAddress bufferAddress(Ctx&, const Buffer&)` | Needs `v12.bufferDeviceAddress`. |
| `VkShaderModule createShaderModule(Ctx&, const uint32_t* code, size_t bytes)` | |
| `void executeImmediate(Ctx&, std::function<void(VkCommandBuffer)>)` | Records, submits and waits. |

## `<bluevk/util.hpp>`

| | |
|---|---|
| `PipelineDesc`, `makeGraphicsPipeline(Ctx&, const PipelineDesc&)` | Dynamic viewport and scissor, counter-clockwise front faces, depth compare GREATER (reversed-Z) by default. No fragment shader: depth only. |
| `blendNone()`, `blendAlpha()`, `blendAdd()`, `blendMultiply()` | Colour attachment blend states. |
| `Image { image, alloc, view }`, `ImageDesc`, `createImage`, `createImageView`, `destroyImage` | Device-local 2D images (an array when `layers > 1`). |
| `attachment(format, load, store, finalLayout, samples = 1)` | An attachment starting from UNDEFINED. |
| `makeRenderPass(Ctx&, atts, colors, depth, deps)` | One subpass: `colors` colour attachments, then an optional depth one. |
| `makeFramebuffer(Ctx&, pass, views, size)` | |
| `makeComputePipeline(Ctx&, module, layout)`, `makeComputePipeline(Ctx&, code, bytes, layout)` | Entry point `main`. |
| `makeSetLayout(Ctx&, {{binding, type}...}, stages)` | |
| `allocSet(Ctx&, layout, pool = VK_NULL_HANDLE)` | From `pool`, or `ctx.descPool` if none is given. |
| `writeBuffer(...)`, `writeImage(...)` | One descriptor each. |
| `groupsFor(n, group = 256)` | Workgroups covering `n` items. |
| `memoryBarrier(...)`, `computeBarrier(cmd)`, `imageBarrier(...)` | |
