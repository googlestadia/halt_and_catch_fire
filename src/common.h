/*
 Copyright 2020 Google Inc.

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#ifndef HCF_COMMON_HEADER
#define HCF_COMMON_HEADER

#include <errno.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using std::vector;

#if defined(_WIN32) || defined(_WIN64)
#define WINDOWS
#endif

#ifndef WINDOWS
#include <pthread.h>
#include <unistd.h>
#endif

#include <vulkan/vulkan.h>

const uint64_t kTestTerminationTimerMsDefault = 120000;

enum class QueueType {
  Undefined,
  Graphics,
  Compute,
  Transfer,
};

// Returns QueueType corrisponding to the string, or default_type if string is
// empty or null. Exit if the string can not be parsed.
QueueType QueueTypeFromString(const char* s,
                              QueueType default_type = QueueType::Graphics);

struct VulkanDevice {
  VkDevice device;
  VkPhysicalDevice physicalDevice;
  std::vector<VkQueue> queues;
  VkQueue queue;  // The default queue to use.

  PFN_vkCmdWriteBufferMarkerAMD CmdWriteBufferMarkerAMD;
  PFN_vkSignalSemaphoreKHR SignalSemaphoreKHR;
  PFN_vkWaitSemaphoresKHR WaitSemaphoresKHR;

  PFN_vkSetDebugUtilsObjectNameEXT SetDebugUtilsObjectNameEXT;

  std::vector<VkCommandPool> commandPools;  // One per queue.
  VkCommandPool commandPool;                // The default CommandPool.
  std::vector<const char*>* deviceExtensions;

  // shader module
  VkShaderModule computeShaderModule;
  VkDescriptorSetLayout descriptorSetLayout;

  // pipeline
  VkPipelineLayout pipelineLayout;
  VkPipeline pipeline;

  // descriptor sets
  VkDescriptorPool descriptorPool;
  VkDescriptorSet descriptorSet;

  // input / output buffers
  VkBuffer bufferIn;
  VkBuffer bufferOut;
  VkDeviceMemory bufferMemory;

  int numBuffers = 2;
  int numBufferEntries = 256;
  int bufferSize = sizeof(float) * numBufferEntries;
  int memorySize = 2 * bufferSize;
};

// This is a single physical device / multiple logical device Vulkan context.
struct VulkanContext {
  VkInstance instance;

  VkPhysicalDevice physicalDevice;
  std::mutex devices_lock;
  std::vector<VulkanDevice> devices;

  uint32_t apiVersion = VK_API_VERSION_1_0;
  std::vector<const char*> instanceExtensions;
  std::vector<const char*> instanceLayers;
  uint64_t test_termination_timer_ms = kTestTerminationTimerMsDefault;

  // If the instance only has one logical device, return that logical device.
  VulkanDevice* GetSingleDevice();

  // Returns the logical device with the given VkDevice handle.
  VulkanDevice* GetDevice(VkDevice vk_device);
};

void LOG(const char* format, ...);

// Initialize a Vulkan context with no device.
bool InitVulkanInstance(VulkanContext* context);

// Initialize a device for the given context, which should already have the
// instance.
VkDevice InitVulkanDevice(VulkanContext* context,
                          std::vector<const char*>* device_extensions = nullptr,
                          const char* shader_module_path = nullptr,
                          std::vector<QueueType>* queues = nullptr);

void SetupWatchdogTimer(VulkanContext* context);

// Initialize a basic single device Vulkan context.
bool InitVulkan(VulkanContext* context,
                std::vector<const char*>* device_extensions = nullptr,
                const char* shader_module_path = nullptr,
                std::vector<QueueType>* queues = nullptr);

#undef None
enum class BufferInitialization {
  None,
  Default,
  MinusOne,
  _64K,
  Transfer,
};

void AllocateInputOutputBuffers(VulkanDevice* device,
                                BufferInitialization initialization);

void CreateDescriptorSets(VulkanDevice* device);

void BeginAndEndCommandBuffer(VkCommandBuffer command_buffer);

void WaitOnEventThatNeverSignals(VulkanDevice* device,
                                 VkCommandBuffer command_buffer);

VkSubmitInfo CreateSubmitInfo(
    const VkCommandBuffer* command_buffer,
    std::vector<VkSemaphore>* wait_semaphores = nullptr,
    std::vector<VkPipelineStageFlags>* wait_dst_stage_masks = nullptr,
    std::vector<VkSemaphore>* signal_semaphores = nullptr,
    void* pnext = nullptr);

void CreateSemaphores(VulkanDevice* device, VkSemaphore* semaphores,
                      uint32_t count, VkSemaphoreTypeKHR type,
                      uint64_t initial_value);

void CreateBinarySemaphores(VulkanDevice* device, VkSemaphore* semaphores,
                            uint32_t count = 1);

void CreateTimelineSemaphores(VulkanDevice* device, VkSemaphore* semaphores,
                              uint32_t count = 1, uint64_t initial_value = 0);

VkTimelineSemaphoreSubmitInfoKHR CreateTimelineSemaphoreSubmitInfo(
    std::vector<uint64_t>* wait_values, std::vector<uint64_t>* signal_values);

VkBindSparseInfo CreateBindSparseInfo(
    std::vector<VkSemaphore>* wait_semaphores,
    std::vector<VkSemaphore>* signal_semaphores, void* pnext);

// Destroys the device object with the given VkHandle
void DeleteVulkanDevice(VulkanContext* context, VkDevice vk_device);

// Destroys the device and the instance of the given context.
void CleanupVulkan(VulkanContext* context);

// Load a SPIRV file and create a new VkShaderModule.
bool LoadShader(VkDevice device, const char* filename, VkShaderModule& shader);

// Returns the memory type index based on the type and properties
// requested.  Return -1 if no appropriate type found.
uint32_t FindMemoryType(VkPhysicalDevice physical_device,
                        uint32_t memoryTypeBits, int memoryProperties);

// Used for validating function pointers returned from vkGetProcAddress.
#define VK_CHECK_FUNCTION_POINTER(f)                                          \
                                                                              \
  {                                                                           \
    auto fp = (f);                                                            \
    if (fp != nullptr) {                                                      \
      LOG("Fatal : Function pointer is nullptr in %s at line %d\n", __FILE__, \
          __LINE__);                                                          \
      assert(fp != nullptr);                                                  \
    }                                                                         \
  }

// Used for validating return values of Vulkan API calls.
#define VK_CHECK_RESULT(f)                                            \
                                                                      \
  {                                                                   \
    VkResult res = (f);                                               \
    if (res != VK_SUCCESS) {                                          \
      LOG("Fatal : VkResult is %d in %s at line %d\n", res, __FILE__, \
          __LINE__);                                                  \
      assert(res == VK_SUCCESS);                                      \
    }                                                                 \
  }

// Used to validate but not assert on Vulkan errors. This is so that
// automated tests don't detect the assert as a test failure.
#define VK_VALIDATE_RESULT(f)                                         \
                                                                      \
  {                                                                   \
    VkResult res = (f);                                               \
    if (res != VK_SUCCESS) {                                          \
      LOG("Fatal : VkResult is %d in %s at line %d\n", res, __FILE__, \
          __LINE__);                                                  \
      exit(0);                                                        \
    }                                                                 \
  }

// Used for validating return values of Vulkan API calls.
#define VK_RETURN_IF_FAIL(f)                                            \
                                                                        \
  {                                                                     \
    VkResult res = (f);                                                 \
    if (res != VK_SUCCESS) {                                            \
      LOG("Warning : VkResult is %d in %s at line %d\n", res, __FILE__, \
          __LINE__);                                                    \
      return res;                                                       \
    }                                                                   \
  }

void DefineFlag(const char* name, const char* help);
void InitFlags(int argc, char** argv);
const char* GetFlag(const char*);

VkResult RunWithCrashCheck(VulkanContext& ctx,
                           std::function<void(VulkanContext&)> f);

VkResult CreateAndRecordCommandBuffers(VulkanDevice* device,
                                       VkCommandBuffer* primary,
                                       VkCommandBuffer* secondary,
                                       std::function<void(VkCommandBuffer)> f,
                                       const char* debug_name = nullptr,

                                       VkCommandPool pool = VK_NULL_HANDLE);

inline VkResult CreateAndRecordCommandBuffers(
    VulkanDevice* device, VkCommandBuffer* primary,
    std::function<void(VkCommandBuffer)> f) {
  return CreateAndRecordCommandBuffers(device, primary, nullptr, f);
}

void SetObjectDebugName(VulkanDevice* device, uint64_t handle,
                        VkObjectType object_type, const char* name);

inline void SetObjectDebugName(VulkanDevice* device, void* handle,
                               VkObjectType object_type, const char* name) {
  return SetObjectDebugName(device, reinterpret_cast<uint64_t>(handle),
                            object_type, name);
}

inline void Initialize() {
#ifdef _HCF_YETI_WIN
  InitializeGGP();
#endif
}

inline void Finalize() {
#ifdef _HCF_YETI_WIN
  FinalizeGGP();
#endif
}

#endif  // HCF_COMMON_HEADER
