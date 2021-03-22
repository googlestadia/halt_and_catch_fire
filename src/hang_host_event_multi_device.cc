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

void TestVulkan(VulkanContext& context, VkDevice vk_device,
                bool run_hang_host_event) {
  auto device = context.GetDevice(vk_device);
  assert(device != nullptr);

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

  // build a second command buffer without a wait event
  VkCommandBufferBeginInfo commandBufferBeginInfo = {};
  commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBeginInfo.flags = 0;

  VK_CHECK_RESULT(
      vkBeginCommandBuffer(commandBuffer2, &commandBufferBeginInfo));

  vkCmdBindPipeline(commandBuffer2, VK_PIPELINE_BIND_POINT_COMPUTE,
                    device->pipeline);

  vkCmdBindDescriptorSets(commandBuffer2, VK_PIPELINE_BIND_POINT_COMPUTE,
                          device->pipelineLayout, 0, 1, &device->descriptorSet,
                          0, nullptr);

  vkCmdDispatch(commandBuffer2, 1, 1, 1);

  VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer2));

  VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    device->pipeline);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          device->pipelineLayout, 0, 1, &device->descriptorSet,
                          0, nullptr);

  vkCmdDispatch(commandBuffer, 1, 1, 1);

  if (run_hang_host_event) {
    WaitOnEventThatNeverSignals(device, commandBuffer);

    // dispatch again to see if the command is executed after the wait event
    vkCmdDispatch(commandBuffer, 1, 1, 1);

    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

    // first we submit a command buffer with a wait event that never gets set
    // we then wait for the queue to execute, then submit another command buffer
    // to detect if we continue executing
    VkSubmitInfo submitInfo = CreateSubmitInfo(&commandBuffer);

    LOG("Submit 1...\n");
    VK_VALIDATE_RESULT(
        vkQueueSubmit(device->queue, 1, &submitInfo, VK_NULL_HANDLE));

    // NOTE: vkQueueWaitIdle will return VK_SUCCESS when this hang is detected
    // instead of returning VK_ERROR_DEVICE_LOST as expected
    LOG("Wait for idle...\n");
    VK_VALIDATE_RESULT(vkQueueWaitIdle(device->queue));

    LOG("Submit 2...\n");
    VkSubmitInfo submitInfo2 = CreateSubmitInfo(&commandBuffer2);
    VK_VALIDATE_RESULT(
        vkQueueSubmit(device->queue, 1, &submitInfo2, VK_NULL_HANDLE));
  }

  LOG("Waiting for idle...\n");
  // NOTE: this vkQueueWaitIdle is not expected to be reached, as a previous
  // Vulkan command is expected to  return VK_ERROR_DEVICE_LOST
  VK_VALIDATE_RESULT(vkQueueWaitIdle(device->queue));
}

// Run our test.
int main(int argc, char* argv[]) {
  Initialize();
  InitFlags(argc, argv);

  VulkanContext context;
  if (!InitVulkanInstance(&context)) {
    return 1;
  }
  VkDevice device1 = InitVulkanDevice(&context, nullptr, "read_write.comp.spv");
  VkDevice device2 = InitVulkanDevice(&context, nullptr, "read_write.comp.spv");
  VkDevice device3 = InitVulkanDevice(&context, nullptr, "read_write.comp.spv");

  if (device1 == VK_NULL_HANDLE || device2 == VK_NULL_HANDLE ||
      device3 == VK_NULL_HANDLE) {
    return 1;
  }
  SetupWatchdogTimer(&context);

  TestVulkan(context, device1, false /* run_hang_host_event */);
  // Intentionally keep this device alive.

  TestVulkan(context, device2, false /* run_hang_host_event */);
  DeleteVulkanDevice(&context, device2);

  TestVulkan(context, device3, true /* run_hang_host_event */);

  Finalize();
  return 0;
}
