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

void PrintUsage() {
  fprintf(stderr,
          "USAGE: [-v vulkan version] [-d device extensions] [-i instance "
          "extension] [-l layer] spriv-file\n");

  fprintf(stderr,
          "\tMultiple extensions and layer can be enabled by passing multiple "
          "-i/-d/-l options\n");
}

// Run our test.
int main(int argc, char* argv[]) {
  VulkanContext context;

  if (argc < 2) {
    PrintUsage();
    exit(-1);
  }

  for (int i = 1; i < argc - 1; ++i) {
    if ((0 == strcmp("-i", argv[i]) || 0 == strcmp("--instance", argv[i])) &&
        (i < argc - 2)) {
      ++i;
      printf("Using instance extension: \"%s\"\n", argv[i]);
      context.instanceExtensions.push_back(argv[i]);
    } else if ((0 == strcmp("-d", argv[i]) ||
                0 == strcmp("--device", argv[i])) &&
               (i < argc - 2)) {
      ++i;
      printf("Using device extension: \"%s\"\n", argv[i]);
      context.GetSingleDevice()->deviceExtensions->push_back(argv[i]);
    } else if ((0 == strcmp("-l", argv[i]) ||
                0 == strcmp("--layer", argv[i])) &&
               (i < argc - 2)) {
      ++i;
      printf("Using instance layers: \"%s\"\n", argv[i]);
      context.instanceLayers.push_back(argv[i]);
    } else if ((0 == strcmp("-v", argv[i]) ||
                0 == strcmp("--version", argv[i])) &&
               (i < argc - 2)) {
      ++i;
      printf("Vulkan version: \"%s\"\n", argv[i]);

      if (0 == strcmp(argv[i], "1.0")) {
        context.apiVersion = VK_API_VERSION_1_0;
      } else if (0 == strcmp(argv[i], "1.1")) {
        context.apiVersion = VK_API_VERSION_1_1;
      } else {
        fprintf(stderr, "Unknown Vulkan version \"%s\"\n", argv[i]);
        exit(-1);
      }
    }
  }

  auto fname = argv[argc - 1];
  printf("Loading shader \"%s\"\n", fname);

  if (!InitVulkan(&context)) {
    return 1;
  }

  VkShaderModule shaderModule;
  LoadShader(context.GetSingleDevice()->device, fname, shaderModule);

  return 0;
}
