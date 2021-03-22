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

  LOG("Creating Buffer Marker Buffer\n");

  // Marker buffer
  VkBuffer markerBuffer;
  VkDeviceMemory markerBufferMemory;
  void* markerBufferPointer;
  {
    VkBufferCreateInfo bufferCreateInfo = {};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = device->bufferSize;
    bufferCreateInfo.usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK_RESULT(
        vkCreateBuffer(vk_device, &bufferCreateInfo, nullptr, &markerBuffer));

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(vk_device, markerBuffer, &memoryRequirements);

    int bufferMemoryType = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = device->memorySize;
    allocateInfo.memoryTypeIndex =
        FindMemoryType(context.physicalDevice,
                       memoryRequirements.memoryTypeBits, bufferMemoryType);

    VK_CHECK_RESULT(vkAllocateMemory(vk_device, &allocateInfo, nullptr,
                                     &markerBufferMemory));

    VK_CHECK_RESULT(
        vkBindBufferMemory(vk_device, markerBuffer, markerBufferMemory, 0));

    // initialize input and output buffers
    {
      VK_CHECK_RESULT(vkMapMemory(vk_device, markerBufferMemory, 0,
                                  VK_WHOLE_SIZE, 0, &markerBufferPointer));

      int* pBufferData = (int*)markerBufferPointer;
      for (int i = 0; i < device->numBufferEntries; ++i) {
        *pBufferData++ = i;
      }
    }
  }

  LOG("INIT MARKERS\n");
  {
    const uint32_t* markers = (uint32_t*)markerBufferPointer;
    for (int i = 0; i < 4; ++i) {
      LOG("%4d: %08x\n", i, markers[i]);
    }
  }

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
  VkCommandBuffer commandBuffer2;
  VK_CHECK_RESULT(vkAllocateCommandBuffers(
      vk_device, &commandBufferAllocateInfo, &commandBuffer2));

  // build a second command buffer
  VkCommandBufferBeginInfo commandBufferBeginInfo = {};
  commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

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

  device->CmdWriteBufferMarkerAMD(commandBuffer,
                                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  markerBuffer, 0, 0xDEADBEEF);

  vkCmdDispatch(commandBuffer, 1, 1, 1);

  // Dispatch twice to see if the command if executed after event
  vkCmdDispatch(commandBuffer, 1, 1, 1);

  device->CmdWriteBufferMarkerAMD(commandBuffer,
                                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                  markerBuffer, 4, 0x0BADF00D);

  VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

  // submit buffer to queue
  VkSubmitInfo submitInfo = CreateSubmitInfo(&commandBuffer);

  for (int i = 0; i < 1; ++i) {
    LOG("Submitting %d\n", i);
    VK_CHECK_RESULT(
        vkQueueSubmit(device->queue, 1, &submitInfo, VK_NULL_HANDLE));
    LOG("Submitted %d\n", i);
  }

  LOG("Waiting for idle...\n");
  VK_CHECK_RESULT(vkQueueWaitIdle(device->queue));

  // Expected program output:
  /*
Creating Buffer Marker Buffer
INIT MARKERS
   0: 00000000
   1: 00000001
   2: 00000002
   3: 00000003
Submitting 0
Submitted 0
Waiting for idle...
MARKERS
   0: deadbeef
   1: 0badf00d
   2: 00000002
   3: 00000003
   */

  // check results
  LOG("MARKERS\n");
  {
    const uint32_t* markers = (uint32_t*)markerBufferPointer;
    for (int i = 0; i < 4; ++i) {
      LOG("%4d: %08x\n", i, markers[i]);
    }
  }
}

// Run our test.
int main(int argc, char* argv[]) {
  Initialize();
  InitFlags(argc, argv);

  VulkanContext context;
  std::vector<const char*> device_extensions;
  device_extensions.push_back(VK_AMD_BUFFER_MARKER_EXTENSION_NAME);
  if (!InitVulkan(&context, &device_extensions, "read_write.comp.spv")) {
    return 1;
  }

  TestVulkan(context);

  Finalize();
  return 0;
}
