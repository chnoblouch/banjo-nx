# The Banjo Programming Language

A small low-level programming language I use for personal projects. The language and toolchain are documented [here](https://chnoblouch.github.io/banjo-lang/). The name still needs some workshopping.

## Features

- Compiling to a binary executable
- Static typing
- Built-in types for tuples, arrays, maps, strings, optionals and results
- Memory management using RAII and "move semantics"
- A hopefully more sane standard library than C++
- Metaprogramming
- Built-in support for unit testing
- Cross-compilation for Linux, Windows and macOS
- Fast compile times
- Performance in the ballpark of C/C++
- Automatic generating of bindings for C libraries
- Built-in language server
- Hot reloading (on Windows)
- Half-baked package management

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

After building, add the `install/bin` directory to your `PATH` variable. Now you can start using the `banjo` command.

## Toolchain

### Compiler

The compiler is written completely from scratch and emits either object files or assembly code depending on the target architecture and operating system. These targets are currently supported:

- `x86_64-windows-msvc`: 64-bit Windows using MSVC (Microsoft Visual Studio) libraries
- `x86_64-windows-gnu`: 64-bit Windows using MinGW (GNU) libraries
- `x86_64-linux-gnu`: x86-64/amd64 Linux using GNU libraries 
- `x86_64-macos`: Intel macOS
- `aarch64-linux-gnu`: AArch64/arm64 Linux using GNU libraries
- `aarch64-macos`: Apple Silicon macOS

The internals of the compiler are documented [here](docs/compiler.md).

### Language Server

The toolchain includes a _language server_ implementing the [Language Server Protocol (LSP)](https://microsoft.github.io/language-server-protocol/). This language server can be integrated into any editor that supports LSP to get features like syntax highlighting and code completion. I've only tested the language server with VS Code on Windows.

The language server currently supports these LSP features:
- Semantic highlighting
- Completion
- Go to definition
- Find references (semi-broken)
- Rename (semi-broken)

### Hot Reloader

The hot reloader is a tool that watches source files for changes, recompiles them, and injects them into a running process. For this to work, the code has to be compiled with support for hot reloading. The compiler generates an _address table_ that contains a list of all functions. Functions are no longer called directly, but indirectly by looking up their address in the address table. This allows the hot reloader to allocate some memory in the target process for functions that have changed, store the freshly compiled code there, and update the pointer in the address table. Future calls to the function will now run the updated code.

## Annoyances

- Some optimization passes are unstable and can produce broken code.
- The AArch64 backend is very incomplete and often generates invalid assembly.
- Values that fit into two registers are passed by reference on the System-V ABI even though they should be passed by splitting them into two registers.

## Directory Structure

- `src`: Main C++ and Python source code
    - `banjo`: Code shared by all tools as a library
        - `ast`: The abstract syntax tree the source code is parsed into
        - `codegen`: Passes for lowering SSA to MCode
        - `config`: Compiler configuration
        - `emit`: Generating assembly and machine code
            - `elf`: Generating ELF binaries (the binary format of Linux)
            - `pe`: Generating PE binaries (the binary format of Windows)
        - `ir`: the SSA intermediate representation
        - `ir_builder`: Generating SSA from AST nodes
        - `lexer`: Breaking source code up into tokens
        - `mcode`: The intermediate representation for target-dependent machine instructions
        - `parser`: Parsing tokens into an AST
        - `passes`: SSA analysis and optimization passes
        - `reports`: Generating errors and warnings
        - `sema`: Semantic analysis of the AST
        - `source`: Data structures for storing source code
        - `symbol`: Symbols attached to the AST
        - `target`: Target-specific backends
            - `aarch64`: aarch64/arm64 backend
            - `x86_64`: x86-64/amd64 backend
        - `utils`: Utility functions and structures
    - `banjo-compiler`: Compiler executable
    - `banjo-lsp`: Language server executable
    - `banjo-hot-reloader`: Hot reloader executable (Windows only)
    - `bindgen`: Tool for generating bindings for C libraries
    - `cli`: CLI tool that wraps all other tools
- `lib`: Libraries used by the compiler and linker
    - `runtime`: A small runtime written in C linked into all Banjo executables
    - `stdlib`: The standard library
- `test`: Test cases
    - `compiled`: Tests written in C++ that have to be compiled first
    - `scripted`: Tests written in Python that invoke the compiler with some input and an expected output 
- `docs`: Online documentation
