# Getting started

## 1. A context

Make the SDL window with `SDL_WINDOW_VULKAN`, then create the context:

```cpp
bvk::ContextDesc desc;
desc.appName = "My Game";
bvk::Ctx ctx;               // or the game's own struct derived from it
ctx.vsync = true;           // read when the swapchain is built
if (!bvk::createContext(ctx, window, desc)) { /* logged */ }
```

You now have `ctx.dev`, `ctx.queue` (one queue for graphics, compute and present), `ctx.vma`, the swapchain (`ctx.swapchain`, `ctx.colorImgs`, `ctx.colorViews`, `ctx.colorFmt`, `ctx.swapExtent`) and the frame's sync objects (`ctx.cmd`, `ctx.acquire`, `ctx.present[]`, `ctx.fence`, made signalled).

Pass `nullptr` for the window to get a headless context, with no surface or swapchain. That's useful for compute work and tests.

## 2. A frame

BlueVK keeps one frame in flight and leaves the loop to the game:

```cpp
vkWaitForFences(ctx.dev, 1, &ctx.fence, VK_TRUE, UINT64_MAX);
VkResult r = vkAcquireNextImageKHR(ctx.dev, ctx.swapchain, UINT64_MAX, ctx.acquire, VK_NULL_HANDLE, &ctx.image);
vkResetFences(ctx.dev, 1, &ctx.fence);
// record ctx.cmd, drawing into the framebuffer for ctx.colorViews[ctx.image]
// submit: wait on ctx.acquire, signal ctx.present[ctx.image], fence ctx.fence
// present: wait on ctx.present[ctx.image]
```

There is one present semaphore per swapchain image, indexed by `ctx.image`, so a semaphore is never reused while the presentation engine still holds it.

## 3. Resizing

Call `ensureSwapchain` once a frame, before drawing. Pass `force = true` after a present returns `VK_ERROR_OUT_OF_DATE_KHR` or `VK_SUBOPTIMAL_KHR`, or after changing `ctx.vsync`:

```cpp
switch (bvk::ensureSwapchain(ctx, window, outOfDate)) {
  case bvk::Swapchain::Rebuilt: /* rebuild framebuffers over ctx.colorViews */ break;
  case bvk::Swapchain::Failed: /* logged */ break;
  case bvk::Swapchain::Unchanged: break;
}
```

A minimised window (0 × 0 pixels) is left alone.

## 4. Resources

```cpp
bvk::Buffer upload;   bvk::createBuffer(ctx, upload, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);  // mapped
bvk::Buffer gpu;      bvk::createDeviceBuffer(ctx, gpu, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
std::memcpy(upload.mapped, data, size);
bvk::executeImmediate(ctx, [&](VkCommandBuffer cmd) {
  VkBufferCopy copy{0, 0, size};
  vkCmdCopyBuffer(cmd, upload.b, gpu.b, 1, &copy);
});

bvk::ImageDesc d;
d.extent = ctx.swapExtent;
d.format = VK_FORMAT_R16G16B16A16_SFLOAT;
d.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
bvk::Image hdr;       bvk::createImage(ctx, d, hdr);  // hdr.view covers all of it
```

## 5. Cleaning up

Destroy the game's own objects (pipelines, passes, framebuffers, images, buffers) while the device still exists, then:

```cpp
bvk::destroyContext(ctx);  // waits for the GPU; ctx back to its defaults
```
