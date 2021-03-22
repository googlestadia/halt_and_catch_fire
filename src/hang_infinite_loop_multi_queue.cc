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

  VkCommandBuffer gfx_primary_cb, gfx_secondary_cb;
  VK_CHECK_RESULT(CreateAndRecordCommandBuffers(
      device, &gfx_primary_cb, &gfx_secondary_cb,
      [device](VkCommandBuffer cb) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->pipeline);

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                device->pipelineLayout, 0, 1,
                                &device->descriptorSet, 0, nullptr);

        vkCmdDispatch(cb, 1, 1, 1);
      },
      "HANG Dispatch Graphics", device->commandPools[0]))

  VkCommandBuffer compute_primary_cb_1, compute_secondary_cb_1;
  VK_CHECK_RESULT(CreateAndRecordCommandBuffers(
      device, &compute_primary_cb_1, &compute_secondary_cb_1,
      [device](VkCommandBuffer cb) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->pipeline);

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                device->pipelineLayout, 0, 1,
                                &device->descriptorSet, 0, nullptr);

        vkCmdDispatch(cb, 1, 1, 1);
      },
      "HANG Dispatch Compute 1", device->commandPools[1]))

  VkCommandBuffer compute_primary_cb_2, compute_secondary_cb_2;
  VK_CHECK_RESULT(CreateAndRecordCommandBuffers(
      device, &compute_primary_cb_2, &compute_secondary_cb_2,
      [device](VkCommandBuffer cb) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, device->pipeline);

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                device->pipelineLayout, 0, 1,
                                &device->descriptorSet, 0, nullptr);

        vkCmdDispatch(cb, 1, 1, 1);
      },
      "HANG Dispatch Compute 2", device->commandPools[1]))

  // we submit a command buffer with a long running compute shader and expect
  // the program to hang and return an error
  VkSubmitInfo gfx_submit_info = CreateSubmitInfo(&gfx_primary_cb);
  VkSubmitInfo compute_submit_info_1 = CreateSubmitInfo(&compute_primary_cb_1);
  VkSubmitInfo compute_submit_info_2 = CreateSubmitInfo(&compute_primary_cb_2);

  LOG("Submit Graphics...\n");
  // NOTE: this should timeout/hang
  VK_VALIDATE_RESULT(
      vkQueueSubmit(device->queues[0], 1, &gfx_submit_info, VK_NULL_HANDLE));

  LOG("Submit Compute 1/2...\n");
  VK_VALIDATE_RESULT(vkQueueSubmit(device->queues[1], 1, &compute_submit_info_1,
                                   VK_NULL_HANDLE));
  LOG("Submit Compute 2/2...\n");
  VK_VALIDATE_RESULT(vkQueueSubmit(device->queues[2], 1, &compute_submit_info_2,
                                   VK_NULL_HANDLE));
}

// Run our test.
int main(int argc, char* argv[]) {
  Initialize();
  InitFlags(argc, argv);

  VulkanContext context;
  if (!InitVulkanInstance(&context)) {
    return 1;
  }
  std::vector<QueueType> queues = {QueueType::Graphics, QueueType::Compute,
                                   QueueType::Compute};
  if (!InitVulkanDevice(&context, nullptr, "infinite_loop.comp.spv", &queues)) {
    return 1;
  }

  VK_CHECK_RESULT(RunWithCrashCheck(context, TestVulkan));

  Finalize();
  return 0;
}
