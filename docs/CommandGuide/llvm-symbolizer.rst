llvm-symbolizer - convert addresses into source code locations
==============================================================

SYNOPSIS
--------

:program:`llvm-symbolizer` [options]

DESCRIPTION
-----------

:program:`llvm-symbolizer` reads object file names and addresses from standard
input and prints corresponding source code locations to standard output. This
program uses debug info sections and symbol table in the object files.

EXAMPLE
--------

.. code-block:: console

  $ cat addr.txt
  a.out 0x4004f4
  /tmp/b.out 0x400528
  /tmp/c.so 0x710
  /tmp/mach_universal_binary:i386 0x1f84
  /tmp/mach_universal_binary:x86_64 0x100000f24
  $ llvm-symbolizer < addr.txt
  main
  /tmp/a.cc:4
  
  f(int, int)
  /tmp/b.cc:11

  h_inlined_into_g
  /tmp/header.h:2
  g_inlined_into_f
  /tmp/header.h:7
  f_inlined_into_main
  /tmp/source.cc:3
  main
  /tmp/source.cc:8

  _main
  /tmp/source_i386.cc:8

  _main
  /tmp/source_x86_64.cc:8

OPTIONS
-------

.. option:: -functions

  Print function names as well as source file/line locations. Defaults to true.

.. option:: -use-symbol-table

 Prefer function names stored in symbol table to function names
 in debug info sections. Defaults to true.

.. option:: -demangle

 Print demangled function names. Defaults to true.

.. option:: -inlining 

 If a source code location is in an inlined function, prints all the
 inlnied frames. Defaults to true.

.. option:: -default-arch

 If a binary contains object files for multiple architectures (e.g. it is a
 Mach-O universal binary), symbolize the object file for a given architecture.
 You can also specify architecture by writing ``binary_name:arch_name`` in the
 input (see example above). If architecture is not specified in either way,
 address will not be symbolized. Defaults to empty string.

EXIT STATUS
-----------

:program:`llvm-symbolizer` returns 0. Other exit codes imply internal program error.
