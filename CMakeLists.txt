# Copyright (C) 2020 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.9.0)
project(halt_and_catch_fire LANGUAGES CXX C)

# Detect GGP
# On Windows GGP_TOOLCHAIN_PATH is not set? But GGP is?
if (GGP OR GGP_TOOLCHAIN_PATH)
  set(GGP TRUE)
  message("GGP TOOLCHAIN PATH ${GGP_TOOLCHAIN_PATH}")
  message("GGP SYSROOT PATH ${GGP_SYSROOT_PATH}")
endif()

# Detect Linux
if(UNIX AND NOT APPLE)
  set(LINUX TRUE)
endif()

# Search libraries only under *target* paths.
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Determine Vulkan's include directory
if (NOT VULKAN_INCLUDE_DIR)
  if (LINUX)
    if (GGP)
      set(VULKAN_INCLUDE_DIR "${GGP_SYSROOT_PATH}/usr/include/vulkan12")
    else()
      if (DEFINED ENV{VULKAN_SDK})
        set(VULKAN_INCLUDE_DIR "$ENV{VULKAN_SDK}/x86_64/include")
      endif()
    endif()
  elseif (WIN32)
    if (DEFINED ENV{VULKAN_SDK})
      set(VULKAN_INCLUDE_DIR "$ENV{VULKAN_SDK}/Include")
    endif()
  endif()
endif()

# Determine Vulkan's library directory
if (NOT VULKAN_LIBRARY_DIR)
  if (LINUX)
    if (GGP)
      set(VULKAN_LIBRARY_DIR "${GGP_SYSROOT_PATH}/usr/local/lib")
    else()
      if (DEFINED ENV{VULKAN_SDK})
        set(VULKAN_LIBRARY_DIR "$ENV{VULKAN_SDK}/x86_64/lib")
      endif()
    endif()
  elseif (WIN32)
    if (DEFINED ENV{VULKAN_SDK})
      set(VULKAN_LIBRARY_DIR "$ENV{VULKAN_SDK}/Lib")
    endif()
  endif()
endif()

# Bail if Vulkan's include directory is not set
if (NOT VULKAN_INCLUDE_DIR)
  message(FATAL_ERROR "VULKAN_INCLUDE_DIR not specified and could not be determined using environment variable VULKAN_SDK")
endif()

# Bail if Vulkan's library directory is not set
if (NOT VULKAN_LIBRARY_DIR)
  message(FATAL_ERROR "VULKAN_LIBRARY_DIR not specified and could not be determined using environment variable VULKAN_SDK")
endif()

message(STATUS "Vulkan Found ${VULKAN_INCLUDE_DIR}")
message(STATUS "Vulkan Found ${VULKAN_LIBRARY_DIR}")

#
# Custom shader target
#
function(add_shader TARGET SHADER)
  find_program(GLSLC glslc)

  set(SHADER_IN ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER})
  get_filename_component(SHADER_FILE ${SHADER} NAME)
  set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_FILE}.spv)
  set(SHADER_INC ${ARGN})

  get_filename_component(SHADER_OUT_DIR "${SHADER_OUT}" DIRECTORY)
  file(MAKE_DIRECTORY ${SHADER_OUT_DIR})
  add_custom_command(
		OUTPUT ${SHADER_OUT}
		COMMAND ${GLSLC} -o ${SHADER_OUT} ${SHADER_IN}
		DEPENDS ${SHADER_IN} ${SHADER_INC}
		IMPLICIT_DEPENDS CXX ${SHADER_IN}
		VERBATIM)

	# Make sure our native build depends on this output.
	set_source_files_properties(${SHADER_OUT} PROPERTIES GENERATED TRUE)
	message(STATUS "Add Shader ${SHADER_OUT} ${SHADER_IN}")
	target_sources(${TARGET} PRIVATE ${SHADER_OUT})
endfunction(add_shader)


if(WIN32)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
message(STATUS "WINDOWS")
elseif(GGP)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_GGP")
message(STATUS "GGP")
else(WIN32)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_XLIB_KHR")
message(STATUS "LINUX")
endif(WIN32)

if (LINUX)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif(LINUX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "${VULKAN_LIBRARY_DIR}")
link_directories(${VULKAN_LIBRARY_DIR})
include_directories(${VULKAN_INCLUDE_DIR})

if (GGP)
if (CMAKE_HOST_WIN32)
target_link_libraries(common "${GGP_LIBRARY_PATH}/lib/libggp.so")
else()
target_link_libraries(common "${GGP_LIBRARY_PATH}/libggp.so")
endif(CMAKE_HOST_WIN32)
endif(GGP)

# Create our common library
add_library(common src/common.h src/common.cc)
add_shader(common src/crash_compute.comp)
add_shader(common src/infinite_loop.comp)
add_shader(common src/read_write.comp)

if (WIN32)
target_link_libraries(common vulkan-1)
else()
target_link_libraries(common vulkan)
endif(WIN32)

# Define our test executables
add_executable(crash_copy src/crash_copy.cc)
target_link_libraries(crash_copy common)

add_executable(crash_shader src/crash_shader_freed_memory.cc)
target_link_libraries(crash_shader common)

add_executable(hang_infinite_loop src/hang_infinite_loop.cc)
target_link_libraries(hang_infinite_loop common)

add_executable(hang_multi_queue src/hang_infinite_loop_multi_queue.cc)
target_link_libraries(hang_multi_queue common)

add_executable(hang_host_event src/hang_host_event.cc)
target_link_libraries(hang_host_event common)

add_executable(hang_host_event_multi_context src/hang_host_event_multi_context.cc)
target_link_libraries(hang_host_event_multi_context common)

add_executable(hang_host_event_multi_device src/hang_host_event_multi_device.cc)
target_link_libraries(hang_host_event_multi_device common)

add_executable(hang_host_event_reset src/hang_host_event_reset.cc)
target_link_libraries(hang_host_event_reset common)

add_executable(hang_semaphore src/hang_semaphore.cc)
target_link_libraries(hang_semaphore common)

add_executable(hang_binary_timeline_semaphore_gpu src/hang_binary_timeline_semaphore_gpu.cc)
target_link_libraries(hang_binary_timeline_semaphore_gpu common)

add_executable(hang_binary_timeline_semaphore_gpu_bind_sparse src/hang_binary_timeline_semaphore_gpu_bind_sparse.cc)
target_link_libraries(hang_binary_timeline_semaphore_gpu_bind_sparse common)

add_executable(hang_timeline_semaphore_gpu src/hang_timeline_semaphore_gpu.cc)
target_link_libraries(hang_timeline_semaphore_gpu common)

add_executable(hang_timeline_semaphore_host src/hang_timeline_semaphore_host.cc)
target_link_libraries(hang_timeline_semaphore_host common)
