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

#include <cstring>

#include "common.h"

void TestVulkan(VulkanContext& context) {
  auto device = context.GetSingleDevice();

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
      "HANG Dispatch"))

  // we submit a command buffer with a long running compute shader and expect
  // the program to hang and return an error
  VkSubmitInfo submitInfo = CreateSubmitInfo(&primary_cb);

  LOG("Submit 1...\n");
  // NOTE: this should timeout/hang
  VK_VALIDATE_RESULT(
      vkQueueSubmit(device->queue, 1, &submitInfo, VK_NULL_HANDLE));
}

// Run our test.
int main(int argc, char* argv[]) {
  Initialize();
  InitFlags(argc, argv);

  VulkanContext context;
  if (!InitVulkan(&context, nullptr, "infinite_loop.comp.spv")) {
    return 1;
  }

  VK_CHECK_RESULT(RunWithCrashCheck(context, TestVulkan));

  Finalize();
  return 0;
}
