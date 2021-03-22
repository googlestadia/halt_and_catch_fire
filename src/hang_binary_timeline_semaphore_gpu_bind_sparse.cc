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

  /* TEST - To verify that GFR correctly tracks the semaphore signal operations
  submitted in vkQueueBindSparse, we create multiple binary and timeline
  semaphores, and signal them to different values in a couple of vkQueueSubmit
  and vkQueueBindSparse commands. Then we wait on all of them for a greater
  value in a vkQueueSubmit command. We expect GFR:
  - catch the hang and dump the state, including the semaphores.
  - report all the current values of the waiting semaphores as expected.
  - To further test vkQueueSubmit and vkQueueBindSpare, all the calls use a
  fence.
  */

  LOG("Creating fence...\n");
  VkFenceCreateInfo fenceCreateInfo = {};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence;
  VK_CHECK_RESULT(vkCreateFence(vk_device, &fenceCreateInfo, nullptr, &fence));
  SetObjectDebugName(device, fence, VK_OBJECT_TYPE_COMMAND_BUFFER, "Fence");

  const uint32_t kNumBinarySemaphores = 10;
  const uint32_t kNumTimelineSemaphores = 10;

  LOG("Creating binary semaphores...\n");
  VkSemaphore binarySemaphores[kNumBinarySemaphores];
  CreateBinarySemaphores(device, &binarySemaphores[0], kNumBinarySemaphores);
  for (int i = 0; i < kNumBinarySemaphores; i++) {
    std::string name = "Binary Semaphore " + std::to_string(i);
    SetObjectDebugName(device, binarySemaphores[i], VK_OBJECT_TYPE_SEMAPHORE,
                       name.c_str());
  }

  LOG("Creating timeline semaphores...\n");
  VkSemaphore timelineSemaphores[kNumTimelineSemaphores];
  CreateTimelineSemaphores(device, &timelineSemaphores[0],
                           kNumTimelineSemaphores, 10);
  for (int i = 0; i < kNumTimelineSemaphores; i++) {
    std::string name = "Timeline Semaphore " + std::to_string(i);
    SetObjectDebugName(device, timelineSemaphores[i], VK_OBJECT_TYPE_SEMAPHORE,
                       name.c_str());
  }

  // binary semaphores:    [0  0  0  0  0  0  0  0  0  0]
  // timelines semaphores: [10 10 10 10 10 10 10 10 10 10]

  // Create a VkSubmitInfo that signals some of the semaphores.
  LOG("Creating and submitting VkSubmitInfo...\n");
  std::vector<uint64_t> signalValues{1, 1, 11, 12, 13};
  auto timelineSemaphoreSubmitInfo =
      CreateTimelineSemaphoreSubmitInfo(nullptr /*waitValues*/, &signalValues);

  std::vector<VkSemaphore> semaphores{
      binarySemaphores[0], binarySemaphores[4], timelineSemaphores[0],
      timelineSemaphores[4], timelineSemaphores[8]};
  VkSubmitInfo submitInfo =
      CreateSubmitInfo(&commandBuffer, nullptr, nullptr, &semaphores,
                       &timelineSemaphoreSubmitInfo);

  VK_VALIDATE_RESULT(vkQueueSubmit(device->queue, 1, &submitInfo, fence));
  LOG("Done.\n");

  // binary semaphores:    [1  0  0  0  1  0  0  0  0  0]
  // timelines semaphores: [11 10 10 10 12 10 10 10 13 10]

  LOG("Waiting for fence from vkQueueSubmit...\n");
  auto result =
      vkWaitForFences(vk_device, 1, &fence, true, 30 * 1000 * 1000 * 1000ULL);
  if (result == VK_TIMEOUT) {
    LOG("TIMEOUT\n");
  } else {
    VK_VALIDATE_RESULT(result);
  }
  LOG("Fence signal received.\n");

  LOG("Resetting the fence...\n");
  VK_VALIDATE_RESULT(vkResetFences(vk_device, 1, &fence));

  {
    // Create first VkBindSparseInfo that waits on some of the current semaphore
    // values and signals some of the semaphores.
    LOG("Creating and submitting VkBindSparseInfo1 with fence...\n");
    std::vector<VkSemaphore> waitSemaphoresBind1{binarySemaphores[0],
                                                 timelineSemaphores[8]};
    std::vector<uint64_t> waitValuesBind1{1, 13};

    std::vector<VkSemaphore> signalSemaphoresBind1{
        binarySemaphores[2], binarySemaphores[6], timelineSemaphores[1],
        timelineSemaphores[5], timelineSemaphores[9]};
    std::vector<uint64_t> signalValuesBind1{1, 1, 14, 15, 16};

    auto timelineSemaphoreSubmitInfo1 =
        CreateTimelineSemaphoreSubmitInfo(&waitValuesBind1, &signalValuesBind1);
    VkBindSparseInfo bindSparseInfo1 =
        CreateBindSparseInfo(&waitSemaphoresBind1, &signalSemaphoresBind1,
                             &timelineSemaphoreSubmitInfo1);
    VK_VALIDATE_RESULT(
        vkQueueBindSparse(device->queue, 1, &bindSparseInfo1, fence));
    LOG("Done.\n");

    LOG("Waiting for fence from vkQueueBindSparse1...\n");
    result =
        vkWaitForFences(vk_device, 1, &fence, true, 30 * 1000 * 1000 * 1000ULL);
    if (result == VK_TIMEOUT) {
      LOG("TIMEOUT\n");
    } else {
      VK_VALIDATE_RESULT(result);
    }
    LOG("Fence signal received.\n");

    LOG("Resetting the fence...\n");
    VK_VALIDATE_RESULT(vkResetFences(vk_device, 1, &fence));

    // binary semaphores:    [0  0  1  0  1  0  1  0  0  0]
    // timelines semaphores: [11 14 10 10 12 15 10 10 13 16]
  }

  {
    // Create second VkBindSparseInfo that waits on some of the current
    // semaphore values and signals some of the semaphores.
    LOG("Creating and submitting VkBindSparseInfo2 with fence...\n");
    std::vector<VkSemaphore> waitSemaphoresBind2{binarySemaphores[4],
                                                 timelineSemaphores[8]};
    std::vector<uint64_t> waitValuesBind2{1, 13};

    std::vector<VkSemaphore> signalSemaphoresBind2{
        binarySemaphores[2], binarySemaphores[7], timelineSemaphores[0],
        timelineSemaphores[1], timelineSemaphores[2]};
    std::vector<uint64_t> signalValuesBind2{1, 1, 17, 18, 19};

    auto timelineSemaphoreSubmitInfo2 =
        CreateTimelineSemaphoreSubmitInfo(&waitValuesBind2, &signalValuesBind2);
    VkBindSparseInfo bindSparseInfo2 =
        CreateBindSparseInfo(&waitSemaphoresBind2, &signalSemaphoresBind2,
                             &timelineSemaphoreSubmitInfo2);

    VK_VALIDATE_RESULT(
        vkQueueBindSparse(device->queue, 1, &bindSparseInfo2, fence));
    LOG("Done.\n");

    LOG("Waiting for fence from vkQueueBindSparse2...\n");
    result =
        vkWaitForFences(vk_device, 1, &fence, true, 30 * 1000 * 1000 * 1000ULL);
    if (result == VK_TIMEOUT) {
      LOG("TIMEOUT\n");
    } else {
      VK_VALIDATE_RESULT(result);
    }
    LOG("Fence signal received.\n");

    // LOG("Resetting the fence...\n");
    VK_VALIDATE_RESULT(vkResetFences(vk_device, 1, &fence));

    // binary semaphores:    [0  0  1  0  0  0  1  1  0  0]
    // timelines semaphores: [17 18 19 10 12 15 10 10 13 16]
  }

  LOG("Creating and submitting VkSubmitInfo2 that waits on all the "
      "semaphores...\n");

  std::vector<VkSemaphore> allSemaphores;
  allSemaphores.reserve(20);
  allSemaphores.insert(allSemaphores.end(), &binarySemaphores[0],
                       &binarySemaphores[10]);
  allSemaphores.insert(allSemaphores.end(), &timelineSemaphores[0],
                       &timelineSemaphores[10]);

  std::vector<VkPipelineStageFlags> dstStageMasks(
      20, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);

  std::vector<uint64_t> waitAllTimelineValues(20, 100);
  auto timelineSemaphoreSubmitInfo2 =
      CreateTimelineSemaphoreSubmitInfo(&waitAllTimelineValues, nullptr);

  VkSubmitInfo submitInfo2 =
      CreateSubmitInfo(&commandBuffer, &allSemaphores, &dstStageMasks, nullptr,
                       &timelineSemaphoreSubmitInfo2);

  VK_VALIDATE_RESULT(vkQueueSubmit(device->queue, 1, &submitInfo2, fence));
  LOG("Done.\n");
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
