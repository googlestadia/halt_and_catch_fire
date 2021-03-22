## Synopsis

The Halt And Catch Fire project is a collection Vulkan programs designed to crash or hang the GPU.

These programs are used to tools such as [Graphics Flight Recorder](https://github.com/googlestadia/gfr) to ensure they can effectively detect known classes of defects. Do to the nature of these programs their specific behaviour may differ depending on the specific GPU, driver or OS. Not all programs will crash

This is not an officially supported Google product.

## Building

### Building on Windows

Run cmake:
```
> cmake -Bbuild -DCMAKE_GENERATOR_PLATFORM=x64 -H.
```
Open the solution: `build\halt_and_catch_fire.sln`

Programs should be run from the `build` directory, as shaders (*.spv) are loaded from the workind directory.

### Building on Linux

```
$ cmake -Bbuild -H.
$ cd build
$ make
```

## Targets

### crash_copy
Attempts to crash by performing a DMA copy using unmapped memory (a use-after-free bug).

### crash_shader
Attempts to crash by dispatching a compute shader that writes to unmapped memory (a use-after-free bug).

### hang_infinite_loop
Hangs the GPU by running a compute shader with a long running time (a not-quite inifinite loop).

### hang_multi_queue
Hangs the GPU by running a compute shader with a long running time on multiple queues (a not-quite inifinite loop).

### hang_host_event
Causes a hang by submitting work that waits on an un-set event.

### hang_semaphore
Causes a hang by submitting with a binary wait semaphore that's never signaled.

### hang_semaphore
Causes a hang by submitting with a binary wait semaphore that's never signaled.

## Running a program.

Copy the requires shader and execuatble to the same folder.

hang_host_event requries read_write_shader.comp.spv
hang_semaphore requries read_write_shader.comp.spv
invalid_local_array_access requires invalid_index.comp.spv

A few command line options are available:

Choose which queue to execute on (for samples that use a single queue):

    `--queue [graphics/compute/transfer]

Not all queue types will be available on all devices.


Execute work from a secondary command buffer, if appropriate:

    `--secondary

Add debug names and labels:

    `--debug_utils
