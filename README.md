# Banjo on the Nintendo Switch

This is a fork of my Banjo programming language that supports cross-compiling
homebrew applications/games for the Nintendo Switch. This only works on Linux so
far. For more information about the language itself, see the [main
repository](https://github.com/chnoblouch/banjo-lang/).

## Building the Toolchain

### Prerequisites

- CMake
- Python 3
- A C++ compiler

### Build Commands

```
cmake -Bbuild -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=install
cmake --build build --target install
```

After building, add the `install/bin` directory to your `PATH` variable. Now you
can start using the `banjo` command. To check if everything worked out, run
`banjo targets`. The list should include a target called
`aarch64-nintendoswitch-devkitpro`:

```
$ banjo targets

Available targets:
  - x86_64-windows-msvc
  - x86_64-windows-gnu
  - x86_64-linux-gnu (host)
  - aarch64-linux-gnu
  - aarch64-macos
  - aarch64-nintendoswitch-devkitpro
```

## Creating a New Project

To build Nintendo Switch executables, you need to install the devkitPro
toolchain as explained [here](https://devkitpro.org/wiki/Getting_Started). Make
sure to install the `switch-dev` tools and that the environment variable
`DEVKITPRO` is set to `/opt/devkitpro`.

After that, you can create a new Banjo project by running `banjo new <name>`.
Open `src/main.bnj` in your editor, delete the existing content, and paste the
following snippet:

```
use libnx as nx;

func main() {
    nx.console_init(null);
    nx.pad_configure_input(1, 31);

    var pad: [u8; 56];
    nx.pad_initialize_default(&pad[0]);

    println("Hello, Switch!");

    while nx.applet_main_loop() {
        nx.pad_update(&pad[0]);
        nx.console_update(null);
    }

    nx.console_exit(null);
}
```

To build the executable, run the following command in the project root path:

```
banjo build --target aarch64-nintendoswitch
```

And that's it! The built NRO file can be found at
`out/aarch64-nintendoswitch-devkitpro-debug/<name>.nro`, ready to be installed
on your console!

## Graphics

To put graphics on the display, the toolchain provides (currently very
incomplete) bindings to the [deko3d](https://github.com/devkitPro/deko3d/)
graphics API. Here's an example that clears the screen with a blue color:

```
use libnx as nx;
use deko3d as dk;

const FB_WIDTH: u32 = 1280;
const FB_HEIGHT: u32 = 720;
const FB_MEM_SIZE: u32 = 3932160;

const CMD_MEM_SIZE: u32 = 16 as u32 * 1024;

func main() {
    var device_maker = dk.DeviceMaker.new();
    var device = dk.device_create(&device_maker);

    var image_layout_maker = dk.ImageLayoutMaker.new(device);
    image_layout_maker.flags = dk.ImageFlags.USAGE_RENDER | dk.ImageFlags.USAGE_PRESENT | dk.ImageFlags.HW_COMPRESSION;
    image_layout_maker.format = dk.ImageFormat.RGBA8_UNORM;
    image_layout_maker.dimensions = [FB_WIDTH, FB_HEIGHT, 0];

    var framebuffer_layout: dk.ImageLayout;
    dk.image_layout_initialize(&framebuffer_layout, &image_layout_maker);

    var mem_block_maker = dk.MemBlockMaker.new(device, FB_MEM_SIZE);
    mem_block_maker.flags = dk.MemBlockFlags.GPU_CACHED | dk.MemBlockFlags.IMAGE;
    var framebuffer_mem_block = dk.mem_block_create(&mem_block_maker);

    var framebuffer: dk.Image;
    dk.image_initialize(&framebuffer, &framebuffer_layout, framebuffer_mem_block, 0);
    var swapchain_image: *dk.Image = &framebuffer;

    var window = nx.nwindow_get_default();
    var swapchain_maker = dk.SwapchainMaker.new(device, window, &swapchain_image, 1);
    var swapchain = dk.swapchain_create(&swapchain_maker);

    mem_block_maker = dk.MemBlockMaker.new(device, CMD_MEM_SIZE);
    mem_block_maker.flags = dk.MemBlockFlags.CPU_UNCACHED | dk.MemBlockFlags.GPU_CACHED;
    var cmd_buf_mem_block = dk.mem_block_create(&mem_block_maker);

    var cmd_buf_maker = dk.CmdBufMaker.new(device);
    var cmd_buf = dk.cmd_buf_create(&cmd_buf_maker);

    dk.cmd_buf_add_memory(cmd_buf, cmd_buf_mem_block, 0, CMD_MEM_SIZE);

    var image_view = dk.ImageView.new(&framebuffer);
    dk.cmd_buf_bind_render_target(cmd_buf, &image_view, null);
    var cmd_bind_framebuffer = dk.cmd_buf_finish_list(cmd_buf);

    var viewport = dk.Viewport {
        x: 0.0,
        y: 0.0,
        width: FB_WIDTH as f32,
        height: FB_HEIGHT as f32,
        near: 0.0,
        far: 1.0,
    };

    var scissor = dk.Scissor {
        x: 0,
        y: 0,
        width: FB_WIDTH,
        height: FB_HEIGHT,
    };

    var clear_color: [f32; 4] = [0.2, 0.7, 1.0, 1.0];
    var rasterizer_state = dk.RasterizerState.new();
    var color_state = dk.ColorState.new();
    var color_write_state = dk.ColorWriteState.new();

    dk.cmd_buf_set_viewports(cmd_buf, 0, &viewport, 1);
    dk.cmd_buf_set_scissors(cmd_buf, 0, &scissor, 1);
    dk.cmd_buf_clear_color(cmd_buf, 0, dk.ColorMask.RGBA, &clear_color);
    dk.cmd_buf_bind_rasterizer_state(cmd_buf, &rasterizer_state);
    dk.cmd_buf_bind_color_state(cmd_buf, &color_state);
    dk.cmd_buf_bind_color_write_state(cmd_buf, &color_write_state);
    var cmd_render = dk.cmd_buf_finish_list(cmd_buf);

    var queue_maker = dk.QueueMaker.new(device);
    queue_maker.flags = dk.QueueFlags.GRAPHICS;
    var render_queue = dk.queue_create(&queue_maker);

    while nx.applet_main_loop() {
        var image_slot = dk.queue_acquire_image(render_queue, swapchain);
        dk.queue_submit_commands(render_queue, cmd_bind_framebuffer);
        dk.queue_submit_commands(render_queue, cmd_render);
        dk.queue_present_image(render_queue, swapchain, image_slot);
    }
}
```
