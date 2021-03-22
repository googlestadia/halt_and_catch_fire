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

  // Create two timeline semaphores
  VkSemaphore timeline_semaphore_1, timeline_semaphore_2;
  CreateTimelineSemaphores(device, &timeline_semaphore_1, 1, 0x10);
  SetObjectDebugName(device, timeline_semaphore_1, VK_OBJECT_TYPE_SEMAPHORE,
                     "TimelineSemaphore 1");
  CreateTimelineSemaphores(device, &timeline_semaphore_2, 1, 0x10);
  SetObjectDebugName(device, timeline_semaphore_2, VK_OBJECT_TYPE_SEMAPHORE,
                     "TimelineSemaphore 2");
  // We create two timeline semaphores, 1 and 2:
  // - submit waits on timeline semaphore 1 and is sent to the queue
  // - Host signals timeline semaphore 1, so the submit can execute
  // - Host waits on timeline semaphore 2
  // - Test version: timeline sempahore 2 never gets signalled,
  //   vkWaitSemaphoresKHR never returns, and test times out.
  // - If working version, GPU signals timeline semaphore 2 in submit

  std::vector<uint64_t> wait_signal_value{0x20};
  auto timelineSemaphoreSubmitInfo =
      CreateTimelineSemaphoreSubmitInfo(&wait_signal_value, nullptr);
  // Working version: submit signals timeline_semaphore_2
  // auto timelineSemaphoreSubmitInfo =
  // CreateTimelineSemaphoreSubmitInfo(&wait_signal_value, &wait_signal_value);

  // make submit wait on timeline_semaphore_1
  std::vector<VkSemaphore> semaphores{timeline_semaphore_1};
  std::vector<VkPipelineStageFlags> dstStageMasks{
      VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT};
  VkSubmitInfo submitInfo =
      CreateSubmitInfo(&commandBuffer, &semaphores, &dstStageMasks, nullptr,
                       &timelineSemaphoreSubmitInfo);
  // Working version: submit signals timeline_semaphore_2
  // VkSubmitInfo submitInfo = CreateSubmitInfo(&commandBuffer,
  // &semaphores, &dstStageMasks, &semaphores, &timelineSemaphoreSubmitInfo);

  LOG("Submitting submit info to the queue\n");
  VK_VALIDATE_RESULT(
      vkQueueSubmit(device->queue, 1, &submitInfo, VK_NULL_HANDLE));
  LOG("Submitted VkSubmitInfo to the queue.\n");

  // host signals timeline_semaphore_1
  VkSemaphoreSignalInfoKHR host_signal_info = {};
  host_signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR;
  host_signal_info.semaphore = timeline_semaphore_1;
  host_signal_info.value = 0x20;
  LOG("Host signalling timeline semaphore 1...\n");
  device->SignalSemaphoreKHR(vk_device, &host_signal_info);
  LOG("Timeline semaphore 1 signalled by the host\n");

  // host waits on timeline_semaphore_2
  uint64_t wait_value = 0x20;
  VkSemaphoreWaitInfoKHR wait_info = {};
  wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
  wait_info.semaphoreCount = 1;
  wait_info.pSemaphores = &timeline_semaphore_2;
  wait_info.pValues = &wait_value;
  LOG("Host waiting on timeline semaphore 2...\n");
  VK_VALIDATE_RESULT(
      device->WaitSemaphoresKHR(vk_device, &wait_info, 0xffffffffffffffff));
  LOG("Timeline semaphore 2 signalled.\n");
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
