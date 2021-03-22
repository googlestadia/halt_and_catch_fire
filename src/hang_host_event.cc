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

  AllocateInputOutputBuffers(device, BufferInitialization::Default);

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

        WaitOnEventThatNeverSignals(device, cb);

        // dispatch again to see if the command is executed after the wait event
        vkCmdDispatch(cb, 1, 1, 1);
      },
      "HANG Dispatch and Wait"));

  // Is this strictly necessary or just for checking if everything is okay?
  VkCommandBuffer primary_cb2, secondary_cb2;
  VK_CHECK_RESULT(CreateAndRecordCommandBuffers(
      device, &primary_cb2, &secondary_cb2,
      [device](VkCommandBuffer cb) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->pipeline);

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                device->pipelineLayout, 0, 1,
                                &device->descriptorSet, 0, nullptr);

        vkCmdDispatch(cb, 1, 1, 1);
      },
      "Dispatch for validation"));

  // first we submit a command buffer with a wait event that never gets set
  // we then wait for the queue to execute, then submit another command buffer
  // to detect if we continue executing
  VkSubmitInfo submitInfo = CreateSubmitInfo(&primary_cb);

  LOG("Submit 1...\n");
  VK_VALIDATE_RESULT(
      vkQueueSubmit(device->queue, 1, &submitInfo, VK_NULL_HANDLE));

  // NOTE: vkQueueWaitIdle will return VK_SUCCESS when this hang is detected
  // instead of returning VK_ERROR_DEVICE_LOST as expected
  LOG("Wait for idle...\n");
  VK_VALIDATE_RESULT(vkQueueWaitIdle(device->queue));

  LOG("Submit 2...\n");
  VkSubmitInfo submitInfo2 = CreateSubmitInfo(&primary_cb2);
  VK_VALIDATE_RESULT(
      vkQueueSubmit(device->queue, 1, &submitInfo2, VK_NULL_HANDLE));

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
  if (!InitVulkan(&context, nullptr, "read_write.comp.spv")) {
    return 1;
  }
  LOG("starting the test...");

  VK_CHECK_RESULT(RunWithCrashCheck(context, TestVulkan));

  Finalize();
  return 0;
}
