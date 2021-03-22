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

#include "common.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstring>
#include <map>
#include <string>

using namespace std::chrono_literals;

void LOG(const char* format, ...) {
  va_list args;
  char str[8 * 1024];
  va_start(args, format);
  vsnprintf(str, sizeof(str), format, args);
  va_end(args);

  fprintf(stderr, "%s", str);
}

static void CommonFlags() {
  DefineFlag("--queue",
             "Type of queue to use, can be graphics/compute/transfer.");
  DefineFlag("--secondary", "Use secondary command buffer.");
  DefineFlag("--debug_utils", "Add debug utils names and labels.");
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType,
    uint64_t obj, size_t location, int32_t code, const char* layerPrefix,
    const char* msg, void* userData) {
  LOG("validation layer: %s\n", msg);

  return VK_FALSE;
}

void PrintPhysicalDeviceMemory(VkPhysicalDevice d) {
  VkPhysicalDeviceMemoryProperties memProps;
  vkGetPhysicalDeviceMemoryProperties(d, &memProps);
}

// Watchdog
static std::thread* watchdog_thread;
static std::mutex watchdog_mutex;
static std::condition_variable test_is_finished;
static std::atomic<bool> test_timedout;

void WatchdogTimer(uint64_t test_termination_timer_ms) {
  LOG("Begin test watchdog [%lu ms]\n", test_termination_timer_ms);

  auto duration_ms = test_termination_timer_ms * 1ms;

  std::cv_status status;
  {
    std::unique_lock<std::mutex> lock(watchdog_mutex);
    status = test_is_finished.wait_for(lock, duration_ms);
  }

  if (status == std::cv_status::timeout) {
    test_timedout = true;
    LOG("Test watchdog expired [%lu ms]. Terminating the test.\n",
        test_termination_timer_ms);
    exit(0);
  }
}

void WaitForWatchdogThread() {
  if (test_timedout) {
    return;
  }

  LOG("Waiting for the watchdog thread to finish...\n");
  {
    std::unique_lock<std::mutex> lock(watchdog_mutex);
    test_is_finished.notify_all();
  }
  if (watchdog_thread && watchdog_thread->joinable()) {
    watchdog_thread->join();
  }
  LOG("Done.\n");
}

// Initialize a Vulkan context with no device.
bool InitVulkanInstance(VulkanContext* context) {
// Use validation layers if this is a debug build.
#if defined(_DEBUG)
  // context->instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif

  if (GetFlag("--debug_utils") != nullptr) {
    if (std::find(context->instanceExtensions.begin(),
                  context->instanceExtensions.end(),
                  "VK_EXT_debug_utils") == context->instanceExtensions.end()) {
      context->instanceExtensions.push_back("VK_EXT_debug_utils");
    }
  }
  // VkApplicationInfo allows the programmer to specify some basic information
  // about the program, which can be useful for layers and tools to provide more
  // debug information.
  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pNext = nullptr;
  appInfo.pApplicationName = "Halt And Catch Fire";
  appInfo.applicationVersion = 1;
  appInfo.pEngineName = "halt_and_catch_fire";
  appInfo.engineVersion = 1;
  appInfo.apiVersion = context->apiVersion;

  // VkInstanceCreateInfo is where the programmer specifies the layers and/or
  // extensions that are needed.
  VkInstanceCreateInfo instInfo = {};
  instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instInfo.pNext = nullptr;
  instInfo.flags = 0;
  instInfo.pApplicationInfo = &appInfo;
  instInfo.enabledExtensionCount =
      static_cast<uint32_t>(context->instanceExtensions.size());
  instInfo.ppEnabledExtensionNames = context->instanceExtensions.data();
  instInfo.enabledLayerCount =
      static_cast<uint32_t>(context->instanceLayers.size());
  instInfo.ppEnabledLayerNames = context->instanceLayers.data();

  // Create the Vulkan instance.
  VkResult result = vkCreateInstance(&instInfo, nullptr, &context->instance);
  if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
    LOG("Unable to find a compatible Vulkan Driver.\n");
    return false;
  } else if (result) {
    LOG("Could not create a Vulkan instance (for unknown reasons) [%08d].\n",
        result);
    return false;
  }

  // Setup debug validation callbacks
  VkDebugReportCallbackCreateInfoEXT createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
  createInfo.flags =
      VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
  createInfo.pfnCallback = VulkanDebugCallback;

  return true;
}

uint32_t SelectQueue(VkPhysicalDevice physical_device, QueueType queue_type) {
  // Get device queue properties
  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count,
                                           nullptr);

  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count,
                                           queue_families.data());

  auto queue_itr =
      std::find_if(begin(queue_families), end(queue_families),
                   [=](const VkQueueFamilyProperties& q) {
                     if (QueueType::Compute == queue_type) {
                       return (q.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                              !(q.queueFlags & VK_QUEUE_GRAPHICS_BIT);
                     } else if (QueueType::Transfer == queue_type) {
                       return (q.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                              !(q.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                              !(q.queueFlags & VK_QUEUE_COMPUTE_BIT);
                     } else {
                       return VK_QUEUE_GRAPHICS_BIT ==
                              (q.queueFlags & VK_QUEUE_GRAPHICS_BIT);
                     }
                   });

  auto queue_index = (uint32_t)std::distance(begin(queue_families), queue_itr);

  return queue_index;
}

VkResult DummySetDebugUtilsObjectNameEXT(
    VkDevice device, const VkDebugUtilsObjectNameInfoEXT* pNameInfo) {
  return VK_SUCCESS;
}

// Initialize a device for the given context, which should already have the
// instance.
VkDevice InitVulkanDevice(VulkanContext* context,
                          std::vector<const char*>* device_extensions,
                          const char* shader_module_path,
                          std::vector<QueueType>* queues) {
  std::vector<QueueType> default_queues;
  if (queues == nullptr) {
    default_queues.push_back(QueueTypeFromString(GetFlag("--queue")));
    queues = &default_queues;
  }
  // Create device
  uint32_t numPhysicalDevices = 0;
  VK_CHECK_RESULT(vkEnumeratePhysicalDevices(context->instance,
                                             &numPhysicalDevices, nullptr));
  LOG("%d physical devices\n", numPhysicalDevices);

  vector<VkPhysicalDevice> physicalDevices(numPhysicalDevices);
  VK_CHECK_RESULT(vkEnumeratePhysicalDevices(
      context->instance, &numPhysicalDevices, physicalDevices.data()));

  vector<VkPhysicalDeviceProperties> physicalDeviceProperties;
  for (const auto& d : physicalDevices) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(d, &properties);
    physicalDeviceProperties.push_back(properties);

    LOG("Device: %s\n", properties.deviceName);

    PrintPhysicalDeviceMemory(d);
  }

  auto physicalDevice = physicalDevices.front();
  context->physicalDevice = physicalDevice;

  std::vector<float> queue_priorites(queues->size(), 1.0f);
  vector<VkDeviceQueueCreateInfo> queue_create_infos;
  vector<std::pair<uint32_t, uint32_t> > queue_indices;
  std::map<uint32_t, size_t> create_info_indices;
  for (auto queue_type : *queues) {
    auto queue_family_index = SelectQueue(physicalDevice, queue_type);
    if (create_info_indices.count(queue_family_index) == 0) {
      VkDeviceQueueCreateInfo queue_info = {};
      queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queue_info.queueFamilyIndex = queue_family_index;
      queue_info.queueCount = 0;
      queue_info.pQueuePriorities = queue_priorites.data();
      create_info_indices[queue_family_index] = queue_create_infos.size();
      queue_create_infos.push_back(queue_info);
    }
    size_t create_info_index = create_info_indices[queue_family_index];
    uint32_t queue_index = queue_create_infos[create_info_index].queueCount;
    queue_create_infos[create_info_index].queueCount++;
    queue_indices.push_back(std::make_pair(queue_family_index, queue_index));
  }

  VkDeviceCreateInfo deviceInfo = {};
  deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.pQueueCreateInfos = queue_create_infos.data();
  deviceInfo.queueCreateInfoCount = (uint32_t)queue_create_infos.size();

  if (device_extensions != nullptr) {
    for (const auto& ext : *device_extensions) {
      LOG("Device Extension: \"%s\"\n", ext);
    }
    deviceInfo.ppEnabledExtensionNames = device_extensions->data();
    deviceInfo.enabledExtensionCount =
        static_cast<uint32_t>(device_extensions->size());
  } else {
    LOG("Device Extension: None\n");
  }

  VulkanDevice device;
  if (device_extensions != nullptr &&
      std::find(device_extensions->begin(), device_extensions->end(),
                "VK_KHR_timeline_semaphore") != device_extensions->end()) {
    VkPhysicalDeviceTimelineSemaphoreFeaturesKHR
        physicalDeviceTimelineSemaphoreFeatures = {};
    physicalDeviceTimelineSemaphoreFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR;
    physicalDeviceTimelineSemaphoreFeatures.timelineSemaphore = true;
    deviceInfo.pNext = &physicalDeviceTimelineSemaphoreFeatures;
  }
  VK_CHECK_RESULT(
      vkCreateDevice(physicalDevices[0], &deviceInfo, nullptr, &device.device));
  device.physicalDevice = physicalDevices[0];
  auto vk_device = device.device;
  device.CmdWriteBufferMarkerAMD =
      (PFN_vkCmdWriteBufferMarkerAMD)vkGetDeviceProcAddr(
          vk_device, "vkCmdWriteBufferMarkerAMD");

  device.SignalSemaphoreKHR = (PFN_vkSignalSemaphoreKHR)vkGetDeviceProcAddr(
      vk_device, "vkSignalSemaphoreKHR");

  device.WaitSemaphoresKHR = (PFN_vkWaitSemaphoresKHR)vkGetDeviceProcAddr(
      vk_device, "vkWaitSemaphoresKHR");

  if (GetFlag("--debug_utils") != nullptr) {
    device.SetDebugUtilsObjectNameEXT =
        (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(
            vk_device, "vkSetDebugUtilsObjectNameEXT");
  } else {
    device.SetDebugUtilsObjectNameEXT = DummySetDebugUtilsObjectNameEXT;
  }
  SetObjectDebugName(&device, vk_device, VK_OBJECT_TYPE_DEVICE,
                     "Default Device");
  SetObjectDebugName(&device, context->instance, VK_OBJECT_TYPE_INSTANCE,
                     "Default Instance");
  SetObjectDebugName(&device, context->physicalDevice,
                     VK_OBJECT_TYPE_PHYSICAL_DEVICE, "Default PhysicalDevice");

  if (queue_indices.empty()) {
    std::lock_guard<std::mutex> lock(context->devices_lock);
    context->devices.push_back(device);
    return vk_device;
  }

  for (auto queue_index : queue_indices) {
    VkQueue queue;
    vkGetDeviceQueue(vk_device, queue_index.first, queue_index.second, &queue);
    device.queues.push_back(queue);

    VkCommandPool pool;
    // create the command pool and command buffers
    VkCommandPoolCreateInfo commandPoolCreateInfo = {};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.queueFamilyIndex = queue_index.first;
    VK_CHECK_RESULT(
        vkCreateCommandPool(vk_device, &commandPoolCreateInfo, nullptr, &pool));
    device.commandPools.push_back(pool);
  }

  device.queue = device.queues.front();
  SetObjectDebugName(&device, device.queue, VK_OBJECT_TYPE_QUEUE,
                     "Default Queue");

  device.commandPool = device.commandPools.front();
  SetObjectDebugName(&device, device.commandPool, VK_OBJECT_TYPE_COMMAND_POOL,
                     "Default CommandPool");

  // load shader module
  if (nullptr != shader_module_path) {
    LoadShader(vk_device, shader_module_path, device.computeShaderModule);

    // create descriptor sets
    auto descriptorPoolSizes = vector<VkDescriptorPoolSize>{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
    };

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
    descriptorPoolCreateInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.poolSizeCount =
        (uint32_t)descriptorPoolSizes.size();
    descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();
    descriptorPoolCreateInfo.maxSets = 2;

    VK_CHECK_RESULT(vkCreateDescriptorPool(vk_device, &descriptorPoolCreateInfo,
                                           nullptr, &device.descriptorPool));

    vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings;
    {
      VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {};
      descriptorSetLayoutBinding.binding = 0;
      descriptorSetLayoutBinding.descriptorType =
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      descriptorSetLayoutBinding.descriptorCount = 1;
      descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

      descriptorSetLayoutBindings.push_back(descriptorSetLayoutBinding);

      descriptorSetLayoutBinding.binding = 1;
      descriptorSetLayoutBindings.push_back(descriptorSetLayoutBinding);
    }

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
    descriptorSetLayoutCreateInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.bindingCount =
        (uint32_t)descriptorSetLayoutBindings.size();
    descriptorSetLayoutCreateInfo.pBindings =
        descriptorSetLayoutBindings.data();

    VK_CHECK_RESULT(
        vkCreateDescriptorSetLayout(vk_device, &descriptorSetLayoutCreateInfo,
                                    nullptr, &device.descriptorSetLayout));

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
    pipelineLayoutCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &device.descriptorSetLayout;

    VK_CHECK_RESULT(vkCreatePipelineLayout(vk_device, &pipelineLayoutCreateInfo,
                                           nullptr, &device.pipelineLayout));

    SetObjectDebugName(&device, device.pipelineLayout,
                       VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                       "Default PipelineLayout");

    VkPipelineShaderStageCreateInfo pipelineStageCreateInfo = {};
    pipelineStageCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineStageCreateInfo.module = device.computeShaderModule;
    pipelineStageCreateInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineCreateInfo = {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.flags = 0;  // none
    pipelineCreateInfo.stage = pipelineStageCreateInfo;
    pipelineCreateInfo.layout = device.pipelineLayout;

    VK_CHECK_RESULT(vkCreateComputePipelines(vk_device,
                                             VK_NULL_HANDLE,  // pipeline cache
                                             1,  // pipelines to create
                                             &pipelineCreateInfo, nullptr,
                                             &device.pipeline));
    SetObjectDebugName(&device, device.pipeline, VK_OBJECT_TYPE_PIPELINE,
                       "Default ComputePipeline");
  }

  std::lock_guard<std::mutex> lock(context->devices_lock);
  context->devices.push_back(device);
  return vk_device;
}

void SetupWatchdogTimer(VulkanContext* context) {
  // Set up the watchdog for exiting the test forcefully after
  // test_termination_timer_ms milliseconds
  if (!watchdog_thread) {
    watchdog_thread =
        new std::thread(WatchdogTimer, context->test_termination_timer_ms);
    std::atexit(WaitForWatchdogThread);
  }
}

bool InitVulkan(VulkanContext* context,
                std::vector<const char*>* device_extensions,
                const char* shader_module_path,
                std::vector<QueueType>* queues) {
  if (!InitVulkanInstance(context)) {
    return false;
  }
  if (InitVulkanDevice(context, device_extensions, shader_module_path,
                       queues) == VK_NULL_HANDLE) {
    return false;
  }
  SetupWatchdogTimer(context);
  return true;
}

void AllocateInputOutputBuffers(VulkanDevice* device,
                                BufferInitialization initialization) {
  VkBufferCreateInfo bufferCreateInfo = {};
  bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCreateInfo.size = device->bufferSize;
  bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (initialization == BufferInitialization::Transfer) {
    bufferCreateInfo.usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  }

  auto vk_device = device->device;
  VK_CHECK_RESULT(
      vkCreateBuffer(vk_device, &bufferCreateInfo, nullptr, &device->bufferIn));
  SetObjectDebugName(device, device->bufferIn, VK_OBJECT_TYPE_BUFFER,
                     "Input Buffer");
  if (initialization == BufferInitialization::Transfer) {
    bufferCreateInfo.usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }

  VK_CHECK_RESULT(vkCreateBuffer(vk_device, &bufferCreateInfo, nullptr,
                                 &device->bufferOut));
  SetObjectDebugName(device, device->bufferOut, VK_OBJECT_TYPE_BUFFER,
                     "Output Buffer");

  VkMemoryRequirements memoryRequirements;
  vkGetBufferMemoryRequirements(vk_device, device->bufferIn,
                                &memoryRequirements);

  int bufferMemoryType = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  VkMemoryAllocateInfo allocateInfo = {};
  allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocateInfo.allocationSize = device->memorySize;
  allocateInfo.memoryTypeIndex =
      FindMemoryType(device->physicalDevice, memoryRequirements.memoryTypeBits,
                     bufferMemoryType);

  VK_CHECK_RESULT(vkAllocateMemory(vk_device, &allocateInfo, nullptr,
                                   &device->bufferMemory));
  SetObjectDebugName(device, device->bufferMemory, VK_OBJECT_TYPE_DEVICE_MEMORY,
                     "DeviceMemory for I/O");

  VK_CHECK_RESULT(
      vkBindBufferMemory(vk_device, device->bufferIn, device->bufferMemory, 0));
  VK_CHECK_RESULT(vkBindBufferMemory(vk_device, device->bufferOut,
                                     device->bufferMemory, device->bufferSize));

  if (initialization != BufferInitialization::None) {
    // initialize input and output buffers
    void* pBuffer;
    VK_CHECK_RESULT(vkMapMemory(vk_device, device->bufferMemory, 0,
                                VK_WHOLE_SIZE, 0, &pBuffer));

    if (initialization == BufferInitialization::Default) {
      float* pBufferData = (float*)pBuffer;
      for (int i = 0; i < device->numBufferEntries; ++i) {
        *pBufferData++ = 2 + i * 2.0f;
      }
    } else if (initialization == BufferInitialization::MinusOne) {
      float* pBufferData = (float*)pBuffer;
      for (int i = 0; i < device->numBufferEntries; ++i) {
        *pBufferData++ = -1.0f;
      }
    } else if (initialization == BufferInitialization::_64K) {
      uint32_t* pBufferData = (uint32_t*)pBuffer;
      for (int i = 0; i < device->numBufferEntries; ++i) {
        *pBufferData++ = 65535;
      }
    }

    {
      float* pBufferData = (float*)pBuffer + device->numBufferEntries;
      for (int i = 0; i < device->numBufferEntries; ++i) {
        *pBufferData++ = 0;
      }
    }

    vkUnmapMemory(vk_device, device->bufferMemory);
  }
}

void CreateDescriptorSets(VulkanDevice* device) {
  // create descriptor set
  VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {};
  descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetAllocInfo.descriptorPool = device->descriptorPool;
  descriptorSetAllocInfo.descriptorSetCount = 1;
  descriptorSetAllocInfo.pSetLayouts = &device->descriptorSetLayout;

  auto vk_device = device->device;
  VK_CHECK_RESULT(vkAllocateDescriptorSets(vk_device, &descriptorSetAllocInfo,
                                           &device->descriptorSet));

  SetObjectDebugName(device, device->descriptorSet,
                     VK_OBJECT_TYPE_DESCRIPTOR_SET, "Default DescriptorSet");

  VkBuffer buffers[] = {device->bufferIn, device->bufferOut};
  std::vector<VkDescriptorBufferInfo> bufferInfo(device->numBuffers);
  std::vector<VkWriteDescriptorSet> writeDescriptorSets(device->numBuffers);

  for (int i = 0; i < device->numBuffers; ++i) {
    bufferInfo[i] = {};
    bufferInfo[i].buffer = buffers[i];
    bufferInfo[i].range = VK_WHOLE_SIZE;

    writeDescriptorSets[i] = {};
    writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSets[i].dstSet = device->descriptorSet;
    writeDescriptorSets[i].dstBinding = static_cast<uint32_t>(i);
    writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writeDescriptorSets[i].descriptorCount = 1;
    writeDescriptorSets[i].pBufferInfo = &bufferInfo[i];
  }

  vkUpdateDescriptorSets(vk_device, 2, writeDescriptorSets.data(), 0, nullptr);
}

void BeginAndEndCommandBuffer(VkCommandBuffer command_buffer) {
  VkCommandBufferBeginInfo command_buffer_begin_info = {};
  command_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK_RESULT(
      vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info));
  VK_CHECK_RESULT(vkEndCommandBuffer(command_buffer));
}

void WaitOnEventThatNeverSignals(VulkanDevice* device,
                                 VkCommandBuffer command_buffer) {
  // Insert an event that we never signal
  VkEventCreateInfo eventCreateInfo = {};
  eventCreateInfo.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;

  VkEvent event;
  VK_CHECK_RESULT(
      vkCreateEvent(device->device, &eventCreateInfo, nullptr, &event));

  SetObjectDebugName(device, event, VK_OBJECT_TYPE_EVENT,
                     "Never-signaled Event");
  // We wait on a host-signaled event that is never signaled
  // This should cause a timeout/hang which should get detected eventually
  vkCmdWaitEvents(command_buffer,
                  1,  // eventCount,
                  &event,
                  VK_PIPELINE_STAGE_HOST_BIT,         // srcStageMask,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,  // dstStageMask,
                  0,                                  // memoryBarrierCount,
                  nullptr,                            // pMemoryBarriers,
                  0,        // bufferMemoryBarrierCount,
                  nullptr,  // pBufferMemoryBarriers,
                  0,        // imageMemoryBarrierCount,
                  nullptr   // pImageMemoryBarriers,
  );
}

VkSubmitInfo CreateSubmitInfo(
    const VkCommandBuffer* command_buffer,
    std::vector<VkSemaphore>* wait_semaphores,
    std::vector<VkPipelineStageFlags>* wait_dst_stage_masks,
    std::vector<VkSemaphore>* signal_semaphores, void* pnext) {
  VkSubmitInfo submit_info = {};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = command_buffer;
  if (wait_semaphores) {
    submit_info.waitSemaphoreCount =
        static_cast<uint32_t>(wait_semaphores->size());
    submit_info.pWaitSemaphores = wait_semaphores->data();
    assert(wait_semaphores->size() == wait_dst_stage_masks->size());
    submit_info.pWaitDstStageMask = wait_dst_stage_masks->data();
  }
  if (signal_semaphores) {
    submit_info.signalSemaphoreCount =
        static_cast<uint32_t>(signal_semaphores->size());
    submit_info.pSignalSemaphores = signal_semaphores->data();
  }
  submit_info.pNext = pnext;
  return submit_info;
}

void CreateSemaphores(VulkanDevice* device, VkSemaphore* semaphores,
                      uint32_t count, VkSemaphoreTypeKHR type,
                      uint64_t initial_value) {
  auto vk_device = device->device;
  // binary semaphores
  if (type == VK_SEMAPHORE_TYPE_BINARY_KHR) {
    VkSemaphoreCreateInfo binarySemaphoreCreateInfo = {};
    binarySemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < count; i++) {
      VK_CHECK_RESULT(vkCreateSemaphore(vk_device, &binarySemaphoreCreateInfo,
                                        nullptr, &semaphores[i]));
    }
    return;
  }

  // timeline semaphores
  VkSemaphoreTypeCreateInfoKHR semaphoreTypeCreateInfo = {};
  semaphoreTypeCreateInfo.sType =
      VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
  semaphoreTypeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
  semaphoreTypeCreateInfo.initialValue = initial_value;

  VkSemaphoreCreateInfo timelineSemaphoreCreateInfo = {};
  timelineSemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  timelineSemaphoreCreateInfo.pNext = &semaphoreTypeCreateInfo;

  for (uint32_t i = 0; i < count; i++) {
    VK_CHECK_RESULT(vkCreateSemaphore(vk_device, &timelineSemaphoreCreateInfo,
                                      nullptr, &semaphores[i]));
  }
}

void CreateBinarySemaphores(VulkanDevice* device, VkSemaphore* semaphores,
                            uint32_t count) {
  CreateSemaphores(device, semaphores, count, VK_SEMAPHORE_TYPE_BINARY_KHR, 0);
}

void CreateTimelineSemaphores(VulkanDevice* device, VkSemaphore* semaphores,
                              uint32_t count, uint64_t initial_value) {
  CreateSemaphores(device, semaphores, count, VK_SEMAPHORE_TYPE_TIMELINE_KHR,
                   initial_value);
}

VkTimelineSemaphoreSubmitInfoKHR CreateTimelineSemaphoreSubmitInfo(
    std::vector<uint64_t>* wait_values, std::vector<uint64_t>* signal_values) {
  VkTimelineSemaphoreSubmitInfoKHR timelineSemaphoreSubmitInfo = {};
  timelineSemaphoreSubmitInfo.sType =
      VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR;
  if (wait_values) {
    timelineSemaphoreSubmitInfo.waitSemaphoreValueCount =
        static_cast<uint32_t>(wait_values->size());
    timelineSemaphoreSubmitInfo.pWaitSemaphoreValues = wait_values->data();
  }
  if (signal_values) {
    timelineSemaphoreSubmitInfo.signalSemaphoreValueCount =
        static_cast<uint32_t>(signal_values->size());
    timelineSemaphoreSubmitInfo.pSignalSemaphoreValues = signal_values->data();
  }
  return timelineSemaphoreSubmitInfo;
}

VkBindSparseInfo CreateBindSparseInfo(
    std::vector<VkSemaphore>* wait_semaphores,
    std::vector<VkSemaphore>* signal_semaphores, void* pnext) {
  VkBindSparseInfo bind_sparse_info = {};
  bind_sparse_info.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
  if (wait_semaphores) {
    bind_sparse_info.waitSemaphoreCount =
        static_cast<uint32_t>(wait_semaphores->size());
    bind_sparse_info.pWaitSemaphores = wait_semaphores->data();
  }
  if (signal_semaphores) {
    bind_sparse_info.signalSemaphoreCount =
        static_cast<uint32_t>(signal_semaphores->size());
    bind_sparse_info.pSignalSemaphores = signal_semaphores->data();
  }
  bind_sparse_info.pNext = pnext;
  return bind_sparse_info;
}

// Destroys the device object with the given VkHandle
void DeleteVulkanDevice(VulkanContext* context, VkDevice vk_device) {
  std::lock_guard<std::mutex> lock(context->devices_lock);
  for (auto it = context->devices.begin(); it != context->devices.end(); it++) {
    if (it->device == vk_device) {
      context->devices.erase(it);
      break;
    }
  }
  vkDestroyDevice(vk_device, nullptr);
}

// Destroys the device and the instance of the given context
void CleanupVulkan(VulkanContext* context) {
  std::lock_guard<std::mutex> lock(context->devices_lock);
  for (auto& device : context->devices) {
    vkDestroyDevice(device.device, nullptr);
  }
  context->devices.clear();
  vkDestroyInstance(context->instance, nullptr);
}

VulkanDevice* VulkanContext::GetSingleDevice() {
  assert(devices.size() == 1);
  return &devices[0];
}

VulkanDevice* VulkanContext::GetDevice(VkDevice vk_device) {
  std::lock_guard<std::mutex> lock(devices_lock);
  auto it = find_if(devices.begin(), devices.end(),
                    [&vk_device](const VulkanDevice& device) {
                      return device.device == vk_device;
                    });
  if (it != devices.end()) {
    return &(*it);
  }
  return nullptr;
}

// Creates a VkShaderModule from raw SPRIV.
bool CreateShader(VkDevice device, const uint32_t* code, size_t codeSize,
                  VkShaderModule& shader) {
  VkShaderModuleCreateInfo shaderCreateInfo = {};
  shaderCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shaderCreateInfo.pCode = code;
  shaderCreateInfo.codeSize = codeSize;

  VK_CHECK_RESULT(
      vkCreateShaderModule(device, &shaderCreateInfo, nullptr, &shader));

  return true;
}

// Load a SPIRV file and create a new VkShaderModule.
bool LoadShader(VkDevice device, const char* filename, VkShaderModule& shader) {
  FILE* f = fopen(filename, "rb");
  if (!f) {
    LOG("Invalid File '%s' - %d: %s\n", filename, errno, strerror(errno));
    return false;
  }

  fseek(f, 0, SEEK_END);
  auto fileLen = ftell(f);
  if (-1 == fileLen) {
    LOG("Invalid length '%s' - %d: %s\n", filename, errno, strerror(errno));
    return false;
  }
  fseek(f, 0, SEEK_SET);

  uint8_t* buffer = new uint8_t[fileLen];

  fread(buffer, 1, fileLen, f);
  fclose(f);

  auto result = CreateShader(device, (uint32_t*)buffer, fileLen, shader);

  delete[] buffer;

  return result;
}

uint32_t FindMemoryType(VkPhysicalDevice physical_device,
                        uint32_t memoryTypeBits, int memoryProperties) {
  VkPhysicalDeviceMemoryProperties deviceMemoryProperties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &deviceMemoryProperties);

  for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; ++i) {
    if ((memoryTypeBits & (1 << i)) &&
        ((deviceMemoryProperties.memoryTypes[i].propertyFlags &
          memoryProperties) == memoryProperties))
      return i;
  }

  return -1;
}

struct Flags {
  // Mapping from name to help string.
  std::map<std::string, std::string> names;
  // Defined flags.
  std::map<std::string, std::string> values;
};

static Flags& GlobalFlags() {
  // Do not call distructor on exit, theses are already crashy programs.
  static Flags* flags = new Flags;
  return *flags;
}

void PrintHelpAndExit(const char* flag = nullptr) {
  if (flag != nullptr && std::string("--help") != flag &&
      std::string("-h") != flag) {
    fprintf(stderr, "Invalid flag: %s\n", flag);
  }
  fprintf(stderr, "Flags:\n");
  for (auto& kv : GlobalFlags().names) {
    fprintf(stderr, "  %s: %s\n", kv.first.c_str(), kv.second.c_str());
  }
  exit(EXIT_FAILURE);
}

void DefineFlag(const char* name, const char* help) {
  GlobalFlags().names[name] = help;
}

void InitFlags(int argc, char** argv) {
  CommonFlags();
  for (int i = 1; i < argc; i++) {
    std::string k = argv[i];
    std::string v = "";
    size_t eqidx = k.find('=');
    if (eqidx != std::string::npos) {
      v = k.substr(eqidx + 1);
      k = k.substr(0, eqidx);
    }
    if (GlobalFlags().names.count(k) == 0) {
      PrintHelpAndExit(k.c_str());
    }
    GlobalFlags().values[k] = v;
  }
}

const char* GetFlag(const char* key) {
  if (GlobalFlags().values.count(key) == 0) {
    return nullptr;
  }
  return GlobalFlags().values[key].c_str();
}

QueueType QueueTypeFromString(const char* s, QueueType default_type) {
  if (s == nullptr || std::strcmp(s, "") == 0) {
    return default_type;
  }
  if (std::strcmp(s, "graphics") == 0 || std::strcmp(s, "Graphics") == 0) {
    return QueueType::Graphics;
  }
  if (std::strcmp(s, "compute") == 0 || std::strcmp(s, "Compute") == 0) {
    return QueueType::Compute;
  }
  if (std::strcmp(s, "transfer") == 0 || std::strcmp(s, "Transfer") == 0) {
    return QueueType::Transfer;
  }
  fprintf(stderr, "Unknown queue type: %s\n", s);
  exit(EXIT_FAILURE);
  return QueueType::Undefined;
}

VkResult RunWithCrashCheck(VulkanContext& ctx,
                           std::function<void(VulkanContext&)> f) {
  auto device = ctx.GetSingleDevice();

  VkCommandBufferAllocateInfo allocate_info = {};
  allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocate_info.commandPool = device->commandPool;
  allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate_info.commandBufferCount = 1;

  // Helper command buffer to catch the device lost error
  VkCommandBuffer cb;
  VK_RETURN_IF_FAIL(
      vkAllocateCommandBuffers(device->device, &allocate_info, &cb));

  SetObjectDebugName(device, cb, VK_OBJECT_TYPE_COMMAND_BUFFER,
                     "Hang/crash detection CommandBuffer");

  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  VK_RETURN_IF_FAIL(vkBeginCommandBuffer(cb, &begin_info));
  VK_RETURN_IF_FAIL(vkEndCommandBuffer(cb));

  // Helper fence to catch the device lost error
  VkFence fence;
  {
    VkFenceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_RETURN_IF_FAIL(vkCreateFence(device->device, &info, nullptr, &fence));
    SetObjectDebugName(device, fence, VK_OBJECT_TYPE_FENCE,
                       "Hang/crash detection Fence");
  }

  f(ctx);

  // NOTE: vkQueueWaitIdle will return VK_SUCCESS occasionally
  LOG("Waiting for idle...\n");
  VK_RETURN_IF_FAIL(vkQueueWaitIdle(device->queue));

  // NOTE: this is where an error gets detected by some version of our driver
  LOG("Submit empty command buffer...\n");
  VkSubmitInfo submit_info = CreateSubmitInfo(&cb);
  VK_RETURN_IF_FAIL(vkQueueSubmit(device->queue, 1, &submit_info, fence));

  // 30s should be enough waiting for to detect hang/crash.
  constexpr std::chrono::nanoseconds kFenceTimeout = 30s;

  VK_RETURN_IF_FAIL(vkWaitForFences(device->device, 1, &fence, VK_TRUE,
                                    kFenceTimeout.count()));

  // NOTE: this vkQueueWaitIdle is not expected to be reached, as a previous
  // Vulkan command is expected to return VK_ERROR_DEVICE_LOST
  LOG("[NOT REACHABLE(if crash/hang)] Waiting for idle...\n");
  return vkQueueWaitIdle(device->queue);
}

VkResult AllocateDefaultCommandBuffer(VulkanDevice* device, VkCommandBuffer* cb,
                                      VkCommandBufferLevel level,
                                      VkCommandPool pool = VK_NULL_HANDLE) {
  if (pool == VK_NULL_HANDLE) {
    pool = device->commandPool;
  }
  // Create secondary command buffers
  VkCommandBufferAllocateInfo allocate_info = {};
  allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocate_info.commandPool = pool;
  allocate_info.level = level;
  allocate_info.commandBufferCount = 1;

  return vkAllocateCommandBuffers(device->device, &allocate_info, cb);
}

VkResult CreateAndRecordCommandBuffers(VulkanDevice* device,
                                       VkCommandBuffer* primary,
                                       VkCommandBuffer* secondary,
                                       std::function<void(VkCommandBuffer)> f,
                                       const char* debug_name,
                                       VkCommandPool pool) {
  // Ignore secondary command buffer if we are not instructed to do so.
  if (GetFlag("--secondary") == nullptr) {
    secondary = nullptr;
  }

  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  VK_CHECK_RESULT(AllocateDefaultCommandBuffer(
      device, primary, VK_COMMAND_BUFFER_LEVEL_PRIMARY, pool));

  // Don't name the primary command buffer if we are doing partial naming.
  if (debug_name != nullptr) {
    std::string name = std::string(debug_name) + " Primary Command Buffer";
    SetObjectDebugName(device, *primary, VK_OBJECT_TYPE_COMMAND_BUFFER,
                       name.c_str());
  }

  VkCommandBuffer cb = *primary;
  if (secondary != nullptr) {
    VK_CHECK_RESULT(AllocateDefaultCommandBuffer(
        device, secondary, VK_COMMAND_BUFFER_LEVEL_SECONDARY, pool));
    cb = *secondary;
    if (debug_name != nullptr) {
      std::string name = std::string(debug_name) + " Secondary Command Buffer";
      SetObjectDebugName(device, *secondary, VK_OBJECT_TYPE_COMMAND_BUFFER,
                         name.c_str());
    }
  }

  VK_CHECK_RESULT(vkBeginCommandBuffer(cb, &begin_info));
  f(cb);
  VK_CHECK_RESULT(vkEndCommandBuffer(cb));

  if (secondary != nullptr) {
    VK_CHECK_RESULT(vkBeginCommandBuffer(*primary, &begin_info));
    vkCmdExecuteCommands(*primary, 1, &cb);
    VK_CHECK_RESULT(vkEndCommandBuffer(*primary));
  }
  return VK_SUCCESS;
}

void SetObjectDebugName(VulkanDevice* device, uint64_t handle,
                        VkObjectType object_type, const char* name) {
  if (name == nullptr) {
    return;
  }
  VkDebugUtilsObjectNameInfoEXT info = {};
  info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
  info.objectType = object_type;
  info.objectHandle = handle;
  info.pObjectName = name;
  device->SetDebugUtilsObjectNameEXT(device->device, &info);
}
