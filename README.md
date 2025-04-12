# Banjo on the Nintendo Switch

This is a fork of my Banjo programming language that supports cross-compiling
homebrew applications/games for the Nintendo Switch. For more information about
the language itself, see the [main
repository](https://github.com/chnoblouch/banjo-lang/).

## Building

### Prerequisites

- CMake
- Python 3
- A C++ compiler (I've tested MSVC, Clang on Windows and Linux, GCC, and Apple Clang)

### Build Commands

```
cmake -Bbuild -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=install
cmake --build build --target install
```

After building, add the `install/bin` directory to your `PATH` variable. Now you
can start using the `banjo` command. To check if everything worked out, run
`banjo targets`. The list should include a target called
`aarch64-nintendoswitch-devkitpro`.

## Creating and Building a New Project

To build Nintendo Switch executables, you need to install the devkitPro
toolchain as explained [here](https://devkitpro.org/wiki/Getting_Started). Make
sure you've installed the `switch-dev` tools.

After that, you can create a new Banjo project by running `banjo new <name>`.
Open `src/main.bnj` in your editor, delete the existing content, and paste the
following snippet:

```
use libnx as nx;

native func puts(*u8);

func main() {
    nx.console_init(null);
    nx.pad_configure_input(1, 31);

    var pad: [u8; 56];
    nx.pad_initialize_default(&pad[0]);

    puts("Hello, Switch!");

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
