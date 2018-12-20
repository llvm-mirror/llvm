The Stream-Specialized LLVM Compiler Infrastructure
===================================================

LLVM compiler infrastructure for stream-specialzed architecture family.
Much stuff is still in working in progress.

Installation
------------

To install stream-specialized LLVM compilation tool chain

````sh
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
````

