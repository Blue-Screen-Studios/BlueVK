#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

#include "bluevk/vma.hpp"

struct SDL_Window;

namespace bvk {

// ------------------------------------------------------------------ logging

// Where BlueVK's messages go (the validation layer's too). By default,
// stderr; a game routes them into its own log.
enum class LogLevel { Trace, Info, Warn, Error };
using LogFn = std::function<void(LogLevel, std::string_view)>;
void setLog(LogFn);

// A Vulkan struct zeroed, its sType set.
template <class T>
T vkStruct(VkStructureType type) {
  T s{};
  s.sType = type;
  return s;
}

// ------------------------------------------------------------------ the GPU

// Vulkan 1.0's features and 1.1's to 1.3's, chained in that order (only the
// ones the API version has). Unlinked when stored: link() before handing it
// to Vulkan, valid until the struct is copied or moved.
struct Features {
  VkPhysicalDeviceFeatures2 core = vkStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
  VkPhysicalDeviceVulkan11Features v11 = vkStruct<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
  VkPhysicalDeviceVulkan12Features v12 = vkStruct<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
  VkPhysicalDeviceVulkan13Features v13 = vkStruct<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);

  // The chain's head for `apiVersion`, `tail` (more structs, already
  // chained) after the last.
  VkPhysicalDeviceFeatures2* link(uint32_t apiVersion, void* tail = nullptr);
  void unlink();
};

// A GPU being considered (ContextDesc::pick): what it is and supports.
struct DeviceCandidate {
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties props{};
  VkPhysicalDeviceMemoryProperties memory{};
  uint64_t vramBytes = 0;  // its device-local heaps
  Features supported;      // (unlinked)
  std::vector<VkExtensionProperties> extensions;
  uint32_t apiVersion = 0;  // the context's (ContextDesc::apiVersion)

  bool hasExtension(const char* name) const;
  // An extension's feature or property struct (its sType given), filled in.
  template <class T>
  T features(VkStructureType type) const {
    T f{};
    f.sType = type;
    VkPhysicalDeviceFeatures2 q = vkStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    q.pNext = &f;
    vkGetPhysicalDeviceFeatures2(phys, &q);
    f.pNext = nullptr;
    return f;
  }
  template <class T>
  T properties(VkStructureType type) const {
    T p{};
    p.sType = type;
    VkPhysicalDeviceProperties2 q = vkStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    q.pNext = &p;
    vkGetPhysicalDeviceProperties2(phys, &q);
    p.pNext = nullptr;
    return p;
  }
};

// What the game enables on a candidate it accepts.
struct DeviceChoice {
  Features enable;                      // the core features (all off to start)
  std::vector<const char*> extensions;  // device extensions (the swapchain's is added with a window)
  int64_t score = 0;                    // added to the ranking (discrete, then the most VRAM)

  // An extension's feature struct, enabled with the device (chained after
  // `enable`, in the order added).
  template <class T>
  T& add(VkStructureType type) {
    auto s = std::make_shared<T>();
    s->sType = type;
    chain.push_back(s);
    return *s;
  }
  std::vector<std::shared_ptr<void>> chain;
};

// Accepts a candidate (filling in what to enable) or passes it over (false,
// `why` logged).
using DevicePicker = std::function<bool(const DeviceCandidate&, DeviceChoice&, std::string& why)>;

struct ContextDesc {
  const char* appName = "BlueVK";
  uint32_t appVersion = VK_MAKE_VERSION(0, 1, 0);
  uint32_t apiVersion = VK_API_VERSION_1_3;  // GPUs below it passed over
  bool computeQueue = true;                  // the one queue does compute too
#ifdef NDEBUG
  bool validation = false;
#else
  bool validation = true;  // VK_LAYER_KHRONOS_validation, when installed
#endif
  DevicePicker pick;  // none: any GPU, nothing optional enabled
  // A descriptor pool shared by the game (Ctx::descPool, allocSet), its sets
  // freeable. None: no pool.
  std::vector<VkDescriptorPoolSize> descriptorPool;
  uint32_t descriptorSets = 0;
};

// What the GPU chosen is (for a debug screen, a log).
struct GpuInfo {
  std::string name;
  std::string type;    // discrete / integrated / virtual / cpu / other
  std::string api;     // the highest Vulkan version it supports, "1.3.290"
  std::string driver;  // its driver's name and version, "NVIDIA 580.82.07"
  uint64_t vramBytes = 0;
  bool validation = false;  // VK_LAYER_KHRONOS_validation active
};

// ------------------------------------------------------------------ context

// The device, its one queue, the window's swapchain and one frame in flight.
// A game derives its own from it, adding its render passes and targets.
struct Ctx {
  VkInstance inst = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;  // (none without a window)
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice dev = VK_NULL_HANDLE;
  uint32_t qf = 0;  // the queue family: graphics (+ compute) (+ present)
  VkQueue queue = VK_NULL_HANDLE;
  VmaAllocator vma = VK_NULL_HANDLE;
  VkDescriptorPool descPool = VK_NULL_HANDLE;  // ContextDesc::descriptorPool's
  uint32_t apiVersion = 0;
  VkPhysicalDeviceProperties props{};
  float timestampPeriod = 1.0f;  // ns per timestamp tick
  uint32_t subgroupSize = 32;
  GpuInfo gpu;
  Features enabled;                     // (unlinked)
  std::vector<std::string> extensions;  // the device's, enabled
  bool hasExtension(const char* name) const;

  // The swapchain. Set before it's built (createContext, ensureSwapchain
  // forced): vsync, the formats wanted (the first the surface has; else its
  // first) and the images' usage.
  bool vsync = true;
  std::vector<VkSurfaceFormatKHR> surfaceFormats{{VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
  VkImageUsageFlags swapchainUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat colorFmt = VK_FORMAT_UNDEFINED;
  VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  VkExtent2D swapExtent{};
  std::vector<VkImage> colorImgs;
  std::vector<VkImageView> colorViews;
  uint32_t windowW = 0, windowH = 0;  // the window's pixels when it was built

  // One frame in flight: a command buffer, the acquire semaphore, a present
  // semaphore per swapchain image (indexed by `image`), and the frame fence
  // (made signaled).
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkSemaphore acquire = VK_NULL_HANDLE;
  std::vector<VkSemaphore> present;
  VkFence fence = VK_NULL_HANDLE;
  uint32_t image = 0;
};

// The instance, the GPU (the best that `desc.pick` accepts), the device and
// the rest, and with a window (`win`: SDL's, made with SDL_WINDOW_VULKAN)
// its surface and swapchain. Without one: no surface, no swapchain (compute,
// tests). False (logged): it couldn't.
bool createContext(Ctx&, SDL_Window* win, const ContextDesc& desc);

enum class Swapchain { Unchanged, Rebuilt, Failed };
// Rebuilds the swapchain when the window's pixel size changed (or `force`:
// after VK_ERROR_OUT_OF_DATE_KHR / SUBOPTIMAL, or vsync changed). Waits for
// the GPU first. A minimized (0 x 0) window is left alone. Rebuilt: the
// game's swapchain framebuffers must follow (colorViews are new).
Swapchain ensureSwapchain(Ctx&, SDL_Window* win, bool force = false);

// Everything createContext made (the game's own destroyed first); the Ctx
// back to its defaults.
void destroyContext(Ctx&);

// ------------------------------------------------------------------ resources

struct Buffer {
  VkBuffer b = VK_NULL_HANDLE;
  VmaAllocation alloc = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  void* mapped = nullptr;  // persistently mapped (host-visible buffers only)
};

// Host-visible, persistently mapped, sequentially written by default; pass
// allocInfo for others.
void createBuffer(Ctx&, Buffer&, VkDeviceSize size, VkBufferUsageFlags usage,
                  const VmaAllocationCreateInfo* allocInfo = nullptr);
// Device-local, never mapped.
void createDeviceBuffer(Ctx&, Buffer&, VkDeviceSize size, VkBufferUsageFlags usage);
// Host-visible and cached, for reading GPU results back (mapped).
void createReadbackBuffer(Ctx&, Buffer&, VkDeviceSize size);
void destroyBuffer(Ctx&, Buffer&);
// The device address of a buffer made with SHADER_DEVICE_ADDRESS usage
// (bufferDeviceAddress enabled: VMA's flag follows it).
VkDeviceAddress bufferAddress(Ctx&, const Buffer&);

VkShaderModule createShaderModule(Ctx&, const uint32_t* code, size_t bytes);

// Runs a short, blocking GPU-only command (a one-time upload): a temporary
// command buffer, submitted with its own fence and waited for. The callback
// only records into the buffer given.
void executeImmediate(Ctx&, const std::function<void(VkCommandBuffer)>&);

}  // namespace bvk
