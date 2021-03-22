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

  AllocateInputOutputBuffers(device, BufferInitialization::Transfer);

  VkCommandBuffer primary_cb, secondary_cb;
  VK_CHECK_RESULT(CreateAndRecordCommandBuffers(
      device, &primary_cb, &secondary_cb,
      [device](VkCommandBuffer cb) {
        VkBufferCopy regions = {};
        regions.srcOffset = 0;
        regions.dstOffset = 0;
        regions.size = 4;

        vkCmdCopyBuffer(cb, device->bufferIn, device->bufferOut, 1, &regions);
      },
      "Copy"));

  // Destroy the buffers AND free the memory backing them.
  // Just destroying the buffers doesn't cause a crash, we need the memory
  // freed.
  vkDestroyBuffer(vk_device, device->bufferIn, nullptr);
  vkDestroyBuffer(vk_device, device->bufferOut, nullptr);
  vkFreeMemory(vk_device, device->bufferMemory, nullptr);

  // first we submit a command buffer with a wait event that never gets set
  // we then wait for the queue to execute, then submit another command buffer
  // to detect if we continue executing
  VkSubmitInfo submit_info = CreateSubmitInfo(&primary_cb);

  LOG("Submit 1...\n");
  VK_VALIDATE_RESULT(
      vkQueueSubmit(device->queue, 1, &submit_info, VK_NULL_HANDLE));
}

// Run our test.
int main(int argc, char* argv[]) {
  Initialize();
  InitFlags(argc, argv);

  VulkanContext context;
  if (!InitVulkan(&context, nullptr, nullptr)) {
    return 1;
  }
  LOG("starting the test...");

  VK_CHECK_RESULT(RunWithCrashCheck(context, TestVulkan));

  Finalize();
  return 0;
}
