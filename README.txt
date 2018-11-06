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
$ cmake -G "Unix Makefiles" -DBUILD_SHARED_LIBS=ON ..
$ make -j4 # I do not recommand a too high thread num, which will be out of memory
$ export PATH=/path/to/build/bin:$PATH
````

