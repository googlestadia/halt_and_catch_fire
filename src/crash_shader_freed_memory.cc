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

  AllocateInputOutputBuffers(device, BufferInitialization::_64K);
  CreateDescriptorSets(device);

  VkCommandBuffer primary_cb, secondary_cb;
  VK_CHECK_RESULT(CreateAndRecordCommandBuffers(
      device, &primary_cb, &secondary_cb,
      [device](VkCommandBuffer cb) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->pipeline);

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                device->pipelineLayout, 0, 1,
                                &device->descriptorSet, 0, nullptr);

        vkCmdDispatch(cb, 1, 1, 1);
      },
      "Dispatch"));

  // we submit a command buffer with a long running compute shader and expect
  // the program to hang and return an error
  VkSubmitInfo submit_info = CreateSubmitInfo(&primary_cb);

  // Destroy the buffers AND free the memory backing them.
  // Just destroying the buffers doesn't cause a crash, we need the memory
  // freed.
  vkDestroyBuffer(vk_device, device->bufferIn, nullptr);
  vkDestroyBuffer(vk_device, device->bufferOut, nullptr);
  vkFreeMemory(vk_device, device->bufferMemory, nullptr);

  LOG("Submit 1...\n");
  // NOTE: this should timeout/hang
  VK_VALIDATE_RESULT(
      vkQueueSubmit(device->queue, 1, &submit_info, VK_NULL_HANDLE));

  // NOTE: vkQueueWaitIdle will return VK_SUCCESS when this hang is detected
  // instead of returning VK_ERROR_DEVICE_LOST as expected
  LOG("Wait for idle...\n");
  VK_VALIDATE_RESULT(vkQueueWaitIdle(device->queue));

  LOG("Done.\n");
}

// Run our test.
int main(int argc, char* argv[]) {
  Initialize();
  InitFlags(argc, argv);

  VulkanContext context;
  if (!InitVulkan(&context, nullptr, "crash_compute.comp.spv")) {
    return 1;
  }

  VK_CHECK_RESULT(RunWithCrashCheck(context, TestVulkan));

  Finalize();
  return 0;
}
