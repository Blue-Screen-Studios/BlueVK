#pragma once
#include <array>
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

// A GPU being considered (Config::pick): what it is and supports.
struct DeviceCandidate {
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties props{};
  VkPhysicalDeviceMemoryProperties memory{};
  uint64_t vramBytes = 0;  // its device-local heaps
  Features supported;      // (unlinked)
  std::vector<VkExtensionProperties> extensions;
  uint32_t apiVersion = 0;  // the context's (Config::apiVersion)

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

// The engine's name, given to Vulkan for every application (VkApplicationInfo).
inline constexpr const char* kEngineName = "IBX_BlueVK";
inline constexpr uint32_t kEngineVersion = VK_MAKE_VERSION(0, 1, 0);  // (BlueVK's)

// Frames the CPU may record ahead of the GPU: each its own command buffer,
// acquire semaphore and fence (Ctx::frame the one being recorded), and the
// swapchain at least as many images. Above Single, whatever the game writes
// per frame it keeps that many of.
enum class Buffering : uint32_t { Single = 1, Double = 2, Triple = 3 };

// Which GPUs are considered: any (ranked by VRAM, then the game's score),
// only discrete ones, or discrete ahead of the rest.
enum class GpuTarget { Any, RequireDiscrete, PreferDiscrete };

// What the context is to be (createContext). The GPU chosen is the best one
// that meets all of it; what it can't meet comes back as a ContextResult.
struct Config {
  std::string appName = "BlueVK App";
  uint32_t appVersion = VK_MAKE_VERSION(0, 1, 0);
  uint32_t apiVersion = VK_API_VERSION_1_3;  // GPUs below it passed over
  Buffering buffering = Buffering::Single;
  GpuTarget gpu = GpuTarget::PreferDiscrete;
  bool computeQueue = true;  // the one queue does compute too
#ifdef NDEBUG
  bool validation = false;
#else
  bool validation = true;  // VK_LAYER_KHRONOS_validation, when installed
#endif
  // Required: a GPU without every feature set here, or every extension
  // listed, is passed over; the GPU chosen has them all enabled. (Features
  // of a version above apiVersion: an invalid config.)
  Features requiredFeatures;
  std::vector<std::string> requiredExtensions;
  // The rest (optional features, extensions' own feature structs, a score):
  // the game's own look at each GPU left. None: nothing more.
  DevicePicker pick;
  // A descriptor pool shared by the game (Ctx::descPool, allocSet), its sets
  // freeable. None: no pool.
  std::vector<VkDescriptorPoolSize> descriptorPool;
  uint32_t descriptorSets = 0;
};

// Why a context couldn't be made (createContext), or Ok. For a GPU passed
// over, the reason of the one that came closest (the later in this list).
enum class ContextResult {
  Ok,
  InvalidConfig,          // the config itself: features above its apiVersion
  VulkanUnavailable,      // no Vulkan driver (or loader), or none for the window system
  InstanceFailed,         // vkCreateInstance failed otherwise
  SurfaceFailed,          // the window's surface couldn't be made
  NoGpu,                  // no GPU at all
  NoDiscreteGpu,          // GpuTarget::RequireDiscrete, and none is
  ApiVersionUnsupported,  // no GPU has the config's apiVersion
  NoSuitableQueue,        // no queue for graphics (+ compute) (+ presenting)
  CannotPresent,          // no GPU can present to the window
  MissingExtension,       // a required extension
  MissingFeature,         // a required feature
  BufferingUnsupported,   // the surface can't have as many images as the buffering
  RejectedByPicker,       // the config's pick turned every GPU left down
  DeviceFailed,           // vkCreateDevice failed
  ResourceFailed,         // the allocator, the command pool or buffers, the descriptor pool, sync objects
  SwapchainFailed,        // the first swapchain couldn't be made
};
const char* toString(ContextResult);

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

// The most frames in flight a context may have (Buffering::Triple).
constexpr uint32_t kMaxFramesInFlight = 3;

// The device, its one queue, the window's swapchain and its frames in flight.
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
  VkDescriptorPool descPool = VK_NULL_HANDLE;  // Config::descriptorPool's
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

  // The frames in flight (`framesInFlight` of them): per frame a command
  // buffer, an acquire semaphore and a fence (made signaled); `frame` is the
  // one being recorded (advanceFrame moves on). A present semaphore per
  // swapchain image (indexed by `image`).
  VkCommandPool pool = VK_NULL_HANDLE;
  uint32_t framesInFlight = 1;
  std::array<VkCommandBuffer, kMaxFramesInFlight> cmds{};
  std::array<VkSemaphore, kMaxFramesInFlight> acquires{};
  std::array<VkFence, kMaxFramesInFlight> fences{};
  std::vector<VkSemaphore> present;
  uint32_t frame = 0;
  uint32_t image = 0;
  VkCommandBuffer cmd() const { return cmds[frame]; }
  VkSemaphore acquire() const { return acquires[frame]; }
  VkFence fence() const { return fences[frame]; }
};

// On to the next frame in flight (after presenting).
inline void advanceFrame(Ctx& c) { c.frame = (c.frame + 1) % c.framesInFlight; }

// The instance, the GPU (the best that meets `config`), the device and the
// rest, and with a window (`win`: SDL's, made with SDL_WINDOW_VULKAN) its
// surface and swapchain. Without one: no surface, no swapchain (compute,
// tests). Not Ok: why not (logged; `detail`, when given, the particulars:
// which GPU lacked what), and the Ctx left as it was made (destroyed).
ContextResult createContext(Ctx&, SDL_Window* win, const Config& config, std::string* detail = nullptr);

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
