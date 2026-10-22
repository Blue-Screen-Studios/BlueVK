#include "bluevk/context.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "log.hpp"

namespace bvk {

using detail::log;
using detail::logf;

// ------------------------------------------------------------------ logging

namespace {
LogFn& logFn() {
  static LogFn fn;
  return fn;
}
}  // namespace

void setLog(LogFn fn) { logFn() = std::move(fn); }

void detail::log(LogLevel level, std::string_view msg) {
  if (logFn()) return logFn()(level, msg);
  static const char* const kNames[] = {"trace", "info", "warn", "error"};
  std::fprintf(stderr, "[bluevk %s] %.*s\n", kNames[int(level)], int(msg.size()), msg.data());
}

namespace {

void die(const char* what) { logf(LogLevel::Error, "FAILED: {}", what); }

// ------------------------------------------------------------------ debug

// The validation layer's messages, logged.
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT /*type*/,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*user*/) {
  LogLevel level = LogLevel::Trace;
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    level = LogLevel::Error;
  else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    level = LogLevel::Warn;
  else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    level = LogLevel::Info;
  logf(level, "[Validation] {}", data->pMessage);
  return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo() {
  VkDebugUtilsMessengerCreateInfoEXT ci{};
  ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  ci.pfnUserCallback = debugCallback;
  return ci;
}

std::string versionText(uint32_t v) {
  return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." +
         std::to_string(VK_API_VERSION_PATCH(v));
}

const char* typeText(VkPhysicalDeviceType t) {
  switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
    default: return "other";
  }
}

// ------------------------------------------------------------------ instance

bool createInstance(Ctx& c, SDL_Window* win, const ContextDesc& desc) {
  std::vector<const char*> extensions;
  if (win) {
    uint32_t count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names) {
      die("SDL_Vulkan_GetInstanceExtensions");
      return false;
    }
    extensions.assign(names, names + count);
  }

  // The validation layer, when asked for and installed.
  std::vector<const char*> layers;
  if (desc.validation) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> available(count);
    vkEnumerateInstanceLayerProperties(&count, available.data());
    for (const VkLayerProperties& l : available)
      if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) c.gpu.validation = true;
    if (c.gpu.validation) {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      layers.push_back("VK_LAYER_KHRONOS_validation");
    } else {
      log(LogLevel::Warn, "VK_LAYER_KHRONOS_validation requested but not available. Continuing without debug layers.");
    }
  }

  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = desc.appName;
  app.applicationVersion = desc.appVersion;
  app.pEngineName = "BlueVK";
  app.apiVersion = desc.apiVersion;

  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  ici.ppEnabledExtensionNames = extensions.data();
  ici.enabledLayerCount = static_cast<uint32_t>(layers.size());
  ici.ppEnabledLayerNames = layers.data();
  // Chained into the instance too, so its own creation and destruction are
  // validated.
  const VkDebugUtilsMessengerCreateInfoEXT debugCi = debugMessengerInfo();
  if (c.gpu.validation) ici.pNext = &debugCi;
  if (vkCreateInstance(&ici, nullptr, &c.inst) != VK_SUCCESS) {
    die("vkCreateInstance");
    return false;
  }
  if (c.gpu.validation) {
    auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(c.inst, "vkCreateDebugUtilsMessengerEXT");
    if (!create || create(c.inst, &debugCi, nullptr, &c.debugMessenger) != VK_SUCCESS)
      log(LogLevel::Warn, "Failed to set up the Vulkan debug messenger");
  }

  if (win && !SDL_Vulkan_CreateSurface(win, c.inst, nullptr, &c.surface)) {
    die("SDL_Vulkan_CreateSurface");
    return false;
  }
  return true;
}

// ------------------------------------------------------------------ device

// The queue family: graphics (+ compute) (+ presenting to the surface).
uint32_t pickQueue(VkPhysicalDevice pd, VkSurfaceKHR surface, bool compute) {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, nullptr);
  std::vector<VkQueueFamilyProperties> queues(count);
  vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, queues.data());
  for (uint32_t qi = 0; qi < count; ++qi) {
    if (!(queues[qi].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
    if (compute && !(queues[qi].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
    if (surface) {
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(pd, qi, surface, &present);
      if (!present) continue;
    }
    return qi;
  }
  return UINT32_MAX;
}

struct Pick {
  DeviceCandidate cand;
  DeviceChoice choice;
  uint32_t qf = 0;
  int64_t score = -1;
};

// The GPU: one the game accepts with the queue it needs, discrete ahead of
// integrated, then the most device-local memory (plus the game's score).
bool pickDevice(Ctx& c, const ContextDesc& desc, Pick& best) {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(c.inst, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(c.inst, &count, devices.data());

  std::string reasons;
  for (VkPhysicalDevice pd : devices) {
    Pick p;
    DeviceCandidate& cand = p.cand;
    cand.phys = pd;
    cand.apiVersion = desc.apiVersion;
    vkGetPhysicalDeviceProperties(pd, &cand.props);
    std::string why;
    auto passOver = [&] {
      logf(LogLevel::Info, "GPU {} passed over: {}", cand.props.deviceName, why);
      reasons += std::string(reasons.empty() ? "" : "; ") + cand.props.deviceName + ": " + why;
    };
    if (cand.props.apiVersion < desc.apiVersion) {
      why = "Vulkan " + versionText(cand.props.apiVersion) + " (" + versionText(desc.apiVersion) + " needed)";
      passOver();
      continue;
    }
    p.qf = pickQueue(pd, c.surface, desc.computeQueue);
    if (p.qf == UINT32_MAX) {
      why = desc.computeQueue ? "no graphics + compute queue" : "no graphics queue";
      if (c.surface) why += " presenting to the window";
      passOver();
      continue;
    }

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, nullptr);
    cand.extensions.resize(extCount);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, cand.extensions.data());
    if (c.surface) {
      uint32_t fcount = 0, pcount = 0;
      vkGetPhysicalDeviceSurfaceFormatsKHR(pd, c.surface, &fcount, nullptr);
      vkGetPhysicalDeviceSurfacePresentModesKHR(pd, c.surface, &pcount, nullptr);
      if (fcount == 0 || pcount == 0 || !cand.hasExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        why = "can't present to the window";
        passOver();
        continue;
      }
    }

    vkGetPhysicalDeviceMemoryProperties(pd, &cand.memory);
    for (uint32_t hi = 0; hi < cand.memory.memoryHeapCount; ++hi)
      if (cand.memory.memoryHeaps[hi].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        cand.vramBytes += cand.memory.memoryHeaps[hi].size;
    vkGetPhysicalDeviceFeatures2(pd, cand.supported.link(desc.apiVersion));
    cand.supported.unlink();

    if (desc.pick && !desc.pick(cand, p.choice, why)) {
      passOver();
      continue;
    }

    p.score = 100;
    if (cand.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
      p.score = 1000000;
    else if (cand.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
      p.score = 200;
    p.score += int64_t(cand.vramBytes / (1024 * 1024)) + p.choice.score;
    if (p.score > best.score) best = std::move(p);
  }
  if (best.score < 0) {
    logf(LogLevel::Error, "FAILED: no suitable GPU{}{}", reasons.empty() ? "" : " (", reasons.empty() ? "" : reasons + ")");
    return false;
  }
  return true;
}

bool createDevice(Ctx& c, const ContextDesc& desc) {
  Pick best;
  if (!pickDevice(c, desc, best)) return false;
  c.phys = best.cand.phys;
  c.qf = best.qf;
  c.props = best.cand.props;
  c.timestampPeriod = c.props.limits.timestampPeriod;
  logf(LogLevel::Info, "Selected GPU: {}", c.props.deviceName);

  c.gpu.name = c.props.deviceName;
  c.gpu.type = typeText(c.props.deviceType);
  c.gpu.api = versionText(c.props.apiVersion);
  c.gpu.vramBytes = best.cand.vramBytes;
  if (desc.apiVersion >= VK_API_VERSION_1_1)
    c.subgroupSize = best.cand
                         .properties<VkPhysicalDeviceSubgroupProperties>(
                             VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES)
                         .subgroupSize;
  if (desc.apiVersion >= VK_API_VERSION_1_2) {
    const auto driver =
        best.cand.properties<VkPhysicalDeviceDriverProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES);
    c.gpu.driver = std::string(driver.driverName) + " " + driver.driverInfo;
  }

  // The extensions: the swapchain's (with a window) and the game's, once each.
  std::vector<const char*> exts;
  if (c.surface) exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  for (const char* e : best.choice.extensions)
    if (std::none_of(exts.begin(), exts.end(), [&](const char* x) { return std::strcmp(x, e) == 0; }))
      exts.push_back(e);

  // The features: the core ones, then the game's extension structs.
  void* tail = nullptr;
  for (auto it = best.choice.chain.rbegin(); it != best.choice.chain.rend(); ++it) {
    auto* s = static_cast<VkBaseOutStructure*>(it->get());
    s->pNext = static_cast<VkBaseOutStructure*>(tail);
    tail = s;
  }

  const float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = c.qf;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;

  VkDeviceCreateInfo dci{};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.pNext = best.choice.enable.link(desc.apiVersion, tail);  // (pEnabledFeatures must then be null)
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
  dci.ppEnabledExtensionNames = exts.data();
  const VkResult made = vkCreateDevice(c.phys, &dci, nullptr, &c.dev);
  best.choice.enable.unlink();
  if (made != VK_SUCCESS) {
    die("vkCreateDevice");
    return false;
  }
  vkGetDeviceQueue(c.dev, c.qf, 0, &c.queue);
  c.enabled = best.choice.enable;
  c.extensions.assign(exts.begin(), exts.end());

  VmaAllocatorCreateInfo ai{};
  ai.instance = c.inst;
  ai.device = c.dev;
  ai.physicalDevice = c.phys;
  ai.vulkanApiVersion = desc.apiVersion;
  if (desc.apiVersion >= VK_API_VERSION_1_2 && c.enabled.v12.bufferDeviceAddress)
    ai.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
  if (vmaCreateAllocator(&ai, &c.vma) != VK_SUCCESS) {
    die("vmaCreateAllocator");
    return false;
  }

  VkCommandPoolCreateInfo cpi{};
  cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cpi.queueFamilyIndex = c.qf;
  if (vkCreateCommandPool(c.dev, &cpi, nullptr, &c.pool) != VK_SUCCESS) {
    die("vkCreateCommandPool");
    return false;
  }
  VkCommandBufferAllocateInfo cbai{};
  cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cbai.commandPool = c.pool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  vkAllocateCommandBuffers(c.dev, &cbai, &c.cmd);

  if (!desc.descriptorPool.empty()) {
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpi.maxSets = desc.descriptorSets;
    dpi.poolSizeCount = static_cast<uint32_t>(desc.descriptorPool.size());
    dpi.pPoolSizes = desc.descriptorPool.data();
    if (vkCreateDescriptorPool(c.dev, &dpi, nullptr, &c.descPool) != VK_SUCCESS) {
      die("vkCreateDescriptorPool");
      return false;
    }
  }

  VkSemaphoreCreateInfo vsci{};
  vsci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  vkCreateSemaphore(c.dev, &vsci, nullptr, &c.acquire);
  VkFenceCreateInfo fci{};
  fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  vkCreateFence(c.dev, &fci, nullptr, &c.fence);
  return true;
}

// ---------------------------------------------------------------- swapchain

bool createSwapchain(Ctx& c, SDL_Window* win) {
  uint32_t fcount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(c.phys, c.surface, &fcount, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(fcount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(c.phys, c.surface, &fcount, formats.data());
  uint32_t pcount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(c.phys, c.surface, &pcount, nullptr);
  std::vector<VkPresentModeKHR> modes(pcount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(c.phys, c.surface, &pcount, modes.data());
  if (formats.empty()) return false;

  VkSurfaceFormatKHR fmt = formats[0];
  [&] {
    for (const VkSurfaceFormatKHR& want : c.surfaceFormats)
      for (const VkSurfaceFormatKHR& f : formats)
        if (f.format == want.format && f.colorSpace == want.colorSpace) return void(fmt = f);
  }();

  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
  if (!c.vsync)
    for (VkPresentModeKHR m : modes)
      if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) {
        mode = m;
        break;
      }

  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c.phys, c.surface, &caps);

  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(win, &w, &h);
  if (caps.currentExtent.width != UINT32_MAX) {
    w = (int)caps.currentExtent.width;
    h = (int)caps.currentExtent.height;
  }
  if (w == 0 || h == 0) return false;
  VkExtent2D extent{(uint32_t)w, (uint32_t)h};
  extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
  extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);

  uint32_t imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);
  imageCount = std::max(imageCount, (uint32_t)2);

  VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  for (uint32_t a = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; a; a <<= 1)
    if (caps.supportedCompositeAlpha & a) {
      alpha = (VkCompositeAlphaFlagBitsKHR)a;
      break;
    }

  VkSwapchainCreateInfoKHR sci{};
  sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  sci.surface = c.surface;
  sci.minImageCount = imageCount;
  sci.imageFormat = fmt.format;
  sci.imageColorSpace = fmt.colorSpace;
  sci.imageExtent = extent;
  sci.imageArrayLayers = 1;
  sci.imageUsage = c.swapchainUsage;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = alpha;
  sci.presentMode = mode;
  sci.clipped = VK_TRUE;
  sci.oldSwapchain = c.swapchain;

  VkSwapchainKHR fresh = VK_NULL_HANDLE;
  if (vkCreateSwapchainKHR(c.dev, &sci, nullptr, &fresh) != VK_SUCCESS) return false;
  for (VkImageView v : c.colorViews) vkDestroyImageView(c.dev, v, nullptr);
  c.colorViews.clear();
  if (c.swapchain) vkDestroySwapchainKHR(c.dev, c.swapchain, nullptr);
  c.swapchain = fresh;

  vkGetSwapchainImagesKHR(c.dev, c.swapchain, &imageCount, nullptr);
  c.colorImgs.resize(imageCount);
  vkGetSwapchainImagesKHR(c.dev, c.swapchain, &imageCount, c.colorImgs.data());
  c.colorViews.assign(imageCount, VK_NULL_HANDLE);
  for (uint32_t i = 0; i < imageCount; ++i) {
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = c.colorImgs[i];
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(c.dev, &vi, nullptr, &c.colorViews[i]);
  }

  for (VkSemaphore s : c.present) vkDestroySemaphore(c.dev, s, nullptr);
  c.present.assign(imageCount, VK_NULL_HANDLE);
  VkSemaphoreCreateInfo vsci{};
  vsci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  for (VkSemaphore& s : c.present) vkCreateSemaphore(c.dev, &vsci, nullptr, &s);

  c.colorFmt = fmt.format;
  c.colorSpace = fmt.colorSpace;
  c.swapExtent = extent;
  SDL_GetWindowSizeInPixels(win, &w, &h);
  c.windowW = (uint32_t)w;
  c.windowH = (uint32_t)h;
  return true;
}

}  // namespace

// ------------------------------------------------------------------ features

VkPhysicalDeviceFeatures2* Features::link(uint32_t apiVersion, void* tail) {
  void* next = tail;
  if (apiVersion >= VK_API_VERSION_1_3) {
    v13.pNext = next;
    next = &v13;
  }
  if (apiVersion >= VK_API_VERSION_1_2) {  // (VkPhysicalDeviceVulkan11Features came with 1.2)
    v12.pNext = next;
    v11.pNext = &v12;
    next = &v11;
  }
  core.pNext = next;
  return &core;
}

void Features::unlink() { core.pNext = v11.pNext = v12.pNext = v13.pNext = nullptr; }

bool DeviceCandidate::hasExtension(const char* name) const {
  return std::any_of(extensions.begin(), extensions.end(),
                     [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

bool Ctx::hasExtension(const char* name) const {
  return std::find(extensions.begin(), extensions.end(), name) != extensions.end();
}

// ------------------------------------------------------------------- context

bool createContext(Ctx& c, SDL_Window* win, const ContextDesc& desc) {
  c.apiVersion = desc.apiVersion;
  if (!createInstance(c, win, desc) || !createDevice(c, desc)) return false;
  return !win || createSwapchain(c, win);
}

Swapchain ensureSwapchain(Ctx& c, SDL_Window* win, bool force) {
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(win, &w, &h);
  if (w == 0 || h == 0) return Swapchain::Unchanged;
  if (!force && w == (int)c.windowW && h == (int)c.windowH) return Swapchain::Unchanged;
  vkDeviceWaitIdle(c.dev);
  return createSwapchain(c, win) ? Swapchain::Rebuilt : Swapchain::Failed;
}

void destroyContext(Ctx& c) {
  if (c.dev) {
    vkDeviceWaitIdle(c.dev);
    for (VkImageView v : c.colorViews) vkDestroyImageView(c.dev, v, nullptr);
    if (c.swapchain) vkDestroySwapchainKHR(c.dev, c.swapchain, nullptr);
    if (c.descPool) vkDestroyDescriptorPool(c.dev, c.descPool, nullptr);
    if (c.cmd) vkFreeCommandBuffers(c.dev, c.pool, 1, &c.cmd);
    if (c.pool) vkDestroyCommandPool(c.dev, c.pool, nullptr);
    if (c.acquire) vkDestroySemaphore(c.dev, c.acquire, nullptr);
    for (VkSemaphore s : c.present) vkDestroySemaphore(c.dev, s, nullptr);
    if (c.fence) vkDestroyFence(c.dev, c.fence, nullptr);
    if (c.vma) vmaDestroyAllocator(c.vma);
    vkDestroyDevice(c.dev, nullptr);
  }
  if (c.surface) vkDestroySurfaceKHR(c.inst, c.surface, nullptr);
  if (c.debugMessenger) {
    auto destroy =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(c.inst, "vkDestroyDebugUtilsMessengerEXT");
    if (destroy) destroy(c.inst, c.debugMessenger, nullptr);
  }
  if (c.inst) vkDestroyInstance(c.inst, nullptr);
  c = {};
}

// ------------------------------------------------------------------ resources

void createBuffer(Ctx& c, Buffer& out, VkDeviceSize size, VkBufferUsageFlags usage,
                  const VmaAllocationCreateInfo* allocInfo) {
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo def{};
  def.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  def.usage = VMA_MEMORY_USAGE_AUTO;
  def.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  const VmaAllocationCreateInfo* ai = allocInfo ? allocInfo : &def;
  VmaAllocationInfo info{};
  if (vmaCreateBuffer(c.vma, &bi, ai, &out.b, &out.alloc, &info) != VK_SUCCESS) {
    logf(LogLevel::Error, "vmaCreateBuffer failed ({} bytes)", size);
    out = {};
    return;
  }
  out.size = size;
  out.mapped = info.pMappedData;
}

void createDeviceBuffer(Ctx& c, Buffer& out, VkDeviceSize size, VkBufferUsageFlags usage) {
  VmaAllocationCreateInfo ai{};
  ai.usage = VMA_MEMORY_USAGE_AUTO;
  ai.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  createBuffer(c, out, size, usage, &ai);
}

void createReadbackBuffer(Ctx& c, Buffer& out, VkDeviceSize size) {
  VmaAllocationCreateInfo ai{};
  ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
  ai.usage = VMA_MEMORY_USAGE_AUTO;
  ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  ai.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  createBuffer(c, out, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &ai);
}

void destroyBuffer(Ctx& c, Buffer& out) {
  if (out.b) vmaDestroyBuffer(c.vma, out.b, out.alloc);
  out = {};
}

VkDeviceAddress bufferAddress(Ctx& c, const Buffer& b) {
  VkBufferDeviceAddressInfo info{};
  info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
  info.buffer = b.b;
  return vkGetBufferDeviceAddress(c.dev, &info);
}

VkShaderModule createShaderModule(Ctx& c, const uint32_t* code, size_t bytes) {
  VkShaderModuleCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  ci.codeSize = bytes;
  ci.pCode = code;
  VkShaderModule m = VK_NULL_HANDLE;
  if (vkCreateShaderModule(c.dev, &ci, nullptr, &m) != VK_SUCCESS) die("vkCreateShaderModule");
  return m;
}

void executeImmediate(Ctx& c, const std::function<void(VkCommandBuffer)>& fn) {
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo cbai{};
  cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cbai.commandPool = c.pool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(c.dev, &cbai, &cmd) != VK_SUCCESS) return;

  VkCommandBufferBeginInfo cbbi{};
  cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &cbbi);
  fn(cmd);
  vkEndCommandBuffer(cmd);

  VkFenceCreateInfo fci{};
  fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence = VK_NULL_HANDLE;
  vkCreateFence(c.dev, &fci, nullptr, &fence);

  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(c.queue, 1, &si, fence);
  vkWaitForFences(c.dev, 1, &fence, VK_TRUE, UINT64_MAX);

  vkDestroyFence(c.dev, fence, nullptr);
  vkFreeCommandBuffers(c.dev, c.pool, 1, &cmd);
}

}  // namespace bvk
