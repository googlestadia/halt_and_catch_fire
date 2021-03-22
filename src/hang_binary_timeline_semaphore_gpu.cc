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

void TestVulkan(VulkanContext& context) {
  auto device = context.GetSingleDevice();
  auto vk_device = device->device;

  AllocateInputOutputBuffers(device, BufferInitialization::Default);

  CreateDescriptorSets(device);

  // Create command buffers
  VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
  commandBufferAllocateInfo.sType =
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAllocateInfo.commandPool = device->commandPool;
  commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAllocateInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  VK_CHECK_RESULT(vkAllocateCommandBuffers(
      vk_device, &commandBufferAllocateInfo, &commandBuffer));
  SetObjectDebugName(device, commandBuffer, VK_OBJECT_TYPE_COMMAND_BUFFER,
                     "CommandBuffer 1");
  VkCommandBuffer commandBuffer2;
  VK_CHECK_RESULT(vkAllocateCommandBuffers(
      vk_device, &commandBufferAllocateInfo, &commandBuffer2));
  SetObjectDebugName(device, commandBuffer2, VK_OBJECT_TYPE_COMMAND_BUFFER,
                     "CommandBuffer 2");

  VkCommandBufferBeginInfo commandBufferBeginInfo = {};
  commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    device->pipeline);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          device->pipelineLayout, 0, 1, &device->descriptorSet,
                          0, nullptr);

  vkCmdDispatch(commandBuffer, 1, 1, 1);

  // Dispatch twice to see if the command if executed after event
  vkCmdDispatch(commandBuffer, 1, 1, 1);

  VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

  // TEST - Insert a binary and a timeline semaphore that we never signal.
  // Submits wait on the semaphores and vkQueueWaitIdle never returns.
  VkSemaphore binary_semaphore;
  CreateBinarySemaphores(device, &binary_semaphore);
  SetObjectDebugName(device, binary_semaphore, VK_OBJECT_TYPE_SEMAPHORE,
                     "Binary Semaphore");

  VkSemaphore timeline_semaphore;
  CreateTimelineSemaphores(device, &timeline_semaphore, 1, 10);
  SetObjectDebugName(device, timeline_semaphore, VK_OBJECT_TYPE_SEMAPHORE,
                     "Timeline Semaphore");

  std::vector<VkSemaphore> semaphores{binary_semaphore, timeline_semaphore};
  std::vector<VkPipelineStageFlags> dstStageMasks(
      2, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
  std::vector<uint64_t> wait_values{0, 20};
  auto timelineSemaphoreSubmitInfo =
      CreateTimelineSemaphoreSubmitInfo(&wait_values, nullptr);

  // make submit wait on timeline semaphore that's never signaled
  VkSubmitInfo submitInfo =
      CreateSubmitInfo(&commandBuffer, &semaphores, &dstStageMasks, nullptr,
                       &timelineSemaphoreSubmitInfo);

  for (int i = 0; i < 5; ++i) {
    LOG("Submitting %d\n", i);
    VK_VALIDATE_RESULT(
        vkQueueSubmit(device->queue, 1, &submitInfo, VK_NULL_HANDLE));
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
}

// Run our test.
int main(int argc, char* argv[]) {
  Initialize();
  InitFlags(argc, argv);

  VulkanContext context;
  std::vector<const char*> device_extensions;
  device_extensions.push_back("VK_KHR_timeline_semaphore");
  if (!InitVulkan(&context, &device_extensions, "read_write.comp.spv")) {
    return 1;
  }

  VK_CHECK_RESULT(RunWithCrashCheck(context, TestVulkan));

  Finalize();
  return 0;
}
