The Stream-Specialized LLVM Compiler Infrastructure
===================================================

LLVM compiler infrastructure for stream-specialzed architecture family.
Much stuff is still in working in progress.

Installation
------------

Stream-specialized architectures are all RISCV ISA based, so cross-platform
compilation is required. Thus, the compilation options are slightly different.

```sh
$ git clone --recursive [this repo]
$ cd /path/to/this/repo; mkdir build; cd build
$ cmake -G "Unix Makefiles" \
        -DBUILD_SHARED_LIBS=ON -DLLVM_USE_SPLIT_DWARF=ON \
        -DCMAKE_INSTALL_PREFIX=$SS_TOOLS -DLLVM_OPTIMIZED_TABLEGEN=ON \
        -DLLVM_BUILD_TESTS=False -DLLVM_TARGETS_TO_BUILD="" \
        -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD="RISCV" \
        -DLLVM_DEFAULT_TARGET_TRIPLE=riscv64-unknown-elf \
        -DCMAKE_CROSSCOMPILING=True ..
$ make -j9
```

Hollo world!
------------
It is a little bit tricky to build RISCV binaries with the current LLVM toolchain:
LLVM toolchain so far can only generate codes, but linking is not perfectly supported
yet. Thus, we need to borrow linker from the gnu toolchain.

Clang can only, by default, generate hard-float (floats goes to its specialized
register file) programs, and GNU toolchain, by default, is built with soft-float
library. This may cause an ABI mismatch. Thus, we need to add `--enable-multilib`
when configuring GNU-RISCV build.

Because RISCV is not the native host ISA, when compiling with Clang, `--sysroot` should
be specified to support C/C++ standard libraries.


```C
// main.c
#include <stdio.h>
int main() {
  puts("Hello world!\n");
  return 0;
}
```

```sh
$ clang main.c -O -c --sysroot=/where/riscv/gnu/toolchain/installed
$ riscv-gnu-unknown-elf-gcc main.o -o main -march=rv64imac -mabi=lp64
# Using RISCV emulator or simulator to verify the binary
```
