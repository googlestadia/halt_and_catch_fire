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

#ifndef HCF_GGP_COMMON_HEADER
#define HCF_GGP_COMMON_HEADER

#include <ggp/ggp.h>

#include <chrono>
#include <iostream>
#include <thread>

// Helper macros
#define Log(mstream) \
  LogSimple(__FILE__, __LINE__, __FUNCTION__) << mstream << std::endl
#define Abort(mstream)                        \
  LogSimple(__FILE__, __LINE__, __FUNCTION__) \
      << "fatal: " << mstream << std::endl,   \
      abort()

// Time per frame for 60 fps.
static const uint64_t kTimePerFrame = 16666LL;

// struct app_data: Stores the application instance state.
static struct {
  // GGP Client Handling
  std::unique_ptr<ggp::EventQueue> event_queue;
  ggp::EventHandle stream_state_changed_handle = kGgpInvalidReference;
  bool stream_started = false;

  // Application State
  bool quit = false;
} app_data;

// LogSimple: A simple function for application logging.
static inline std::ostream& LogSimple(const char* file, unsigned line,
                                      const char* func) {
  return std::cout << file << ":" << line << ":" << func << ": ";
}

// HandleStreamStateChanged(): Handle stream state changed event.
static void HandleStreamStateChanged(
    const ggp::StreamStateChangedEvent& event) {
  switch (event.new_state) {
    case ggp::StreamStateChanged::kStarting:
    case ggp::StreamStateChanged::kInvalid:
      // Nothing to do.
      break;
    case ggp::StreamStateChanged::kSuspended:
      Log("client disconnected");
      break;
    case ggp::StreamStateChanged::kStarted:
      Log("client connected");
      app_data.stream_started = true;
      break;
    case ggp::StreamStateChanged::kExited:
      Log("client disconnected");
      app_data.quit = true;
      break;
  }
}

// Initialize(): Initialize the application.
static void InitializeGGP() {
  // Initialize the GGP subsystem.
  ggp::Initialize();

  app_data.event_queue.reset(new ggp::EventQueue);

  // Register client connection handler.
  app_data.stream_state_changed_handle = ggp::AddStreamStateChangedHandler(
      app_data.event_queue.get(), HandleStreamStateChanged);

  // Wait for the client to connect before using any APIs that require an
  // active session.
  while (!app_data.stream_started) app_data.event_queue->ProcessEvent();
}

// Finalize(): Clean up application resources.
static void FinalizeGGP() {
  // Wait until the user closes the window, then exit.
  while (!app_data.quit) {
    const auto whenToResume = std::chrono::high_resolution_clock::now() +
                              std::chrono::microseconds(kTimePerFrame);

    while (app_data.event_queue->ProcessEvent()) {
    }  // empty loop

    // Sleep until the next frame.
    const auto timeLeft =
        whenToResume - std::chrono::high_resolution_clock::now();
    if (timeLeft > std::chrono::high_resolution_clock::duration::zero())
      std::this_thread::sleep_for(timeLeft);
  }

  // Remove client connection handler.
  ggp::RemoveStreamStateChangedHandler(app_data.stream_state_changed_handle);

  // Disconnect the streaming client.
  ggp::StopStream();
}

#endif  // HCF_GGP_COMMON_HEADER
