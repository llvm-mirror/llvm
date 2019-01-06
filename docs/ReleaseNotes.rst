========================
LLVM 8.0.0 Release Notes
========================

.. contents::
    :local:

.. warning::
   These are in-progress notes for the upcoming LLVM 8 release.
   Release notes for previous releases can be found on
   `the Download Page <https://releases.llvm.org/download.html>`_.


Introduction
============

This document contains the release notes for the LLVM Compiler Infrastructure,
release 8.0.0.  Here we describe the status of LLVM, including major improvements
from the previous release, improvements in various subprojects of LLVM, and
some of the current users of the code.  All LLVM releases may be downloaded
from the `LLVM releases web site <https://llvm.org/releases/>`_.

For more information about LLVM, including information about the latest
release, please check out the `main LLVM web site <https://llvm.org/>`_.  If you
have questions or comments, the `LLVM Developer's Mailing List
<https://lists.llvm.org/mailman/listinfo/llvm-dev>`_ is a good place to send
them.

Note that if you are reading this file from a Subversion checkout or the main
LLVM web page, this document applies to the *next* release, not the current
one.  To see the release notes for a specific release, please see the `releases
page <https://llvm.org/releases/>`_.

Non-comprehensive list of changes in this release
=================================================

* The Windows installer no longer includes a Visual Studio integration.
  Instead, a new
  `LLVM Compiler Toolchain Visual Studio extension <https://marketplace.visualstudio.com/items?itemName=LLVMExtensions.llvm-toolchain>`_
  is available on the Visual Studio Marketplace. The new integration
  supports Visual Studio 2017.

* The **llvm-cov** tool can now export lcov trace files using the
  `-format=lcov` option of the `export` command.

* The add_llvm_loadable_module CMake macro has been removed.  The
  add_llvm_library macro with the MODULE argument now provides the same
  functionality.  See `Writing an LLVM Pass
  <WritingAnLLVMPass.html#setting-up-the-build-environment>`_.

* Added support for the ``.rva`` assembler directive for COFF targets.

* The :program:`llvm-rc` tool (Windows Resource Compiler) has been improved
  a bit. There are still known missing features, but it is generally usable
  in many cases. (The tool still doesn't preprocess input files automatically,
  but it can now handle leftover C declarations in preprocessor output, if
  given output from a preprocessor run externally.)

* CodeView debug info can now be emitted for MinGW configurations, if requested.

* The :program:`opt` tool now supports the ``-load-pass-plugin`` option for
  loading pass plugins for the new PassManager.

* Support for profiling JITed code with perf.


Changes to the LLVM IR
----------------------


Changes to the AArch64 Target
-----------------------------

* The ``.inst`` assembler directive is now usable on both COFF and Mach-O
  targets, in addition to ELF.

* Support for most remaining COFF relocations has been added.

* Support for TLS on Windows has been added.

* Assembler and disassembler support for the ARM Scalable Vector Extension has
  been added.

Changes to the ARM Target
-------------------------

* The ``.inst`` assembler directive is now usable on both COFF and Mach-O
  targets, in addition to ELF. For Thumb, it can now also automatically
  deduce the instruction size, without having to specify it with
  e.g. ``.inst.w`` as before.

Changes to the Hexagon Target
-----------------------------

* Hexagon now supports auto-vectorization for HVX. It is disabled by default
  and can be turned on with ``-fvectorize``. For auto-vectorization to take
  effect, code generation for HVX needs to be enabled with ``-mhvx``.
  The complete set of options should include ``-fvectorize``, ``-mhvx``,
  and ``-mhvx-length={64b|128b}``.

* The support for Hexagon ISA V4 is deprecated and will be removed in the
  next release.

Changes to the MIPS Target
--------------------------

During this release the MIPS target has:

* Added support for Virtualization, Global INValidate ASE,
  and CRC ASE instructions.

* Introduced definitions of ``[d]rem``, ``[d]remu``,
  and microMIPSR6 ``ll/sc`` instructions.

* Shrink-wrapping is now supported and enabled by default (except for ``-O0``).

* Extended size reduction pass by the LWP and SWP instructions.

* Gained initial support of GlobalISel instruction selection framework.

* Updated the P5600 scheduler model not to use instruction itineraries.

* Added disassembly support for comparison and fused (negative) multiply
  ``add/sub`` instructions.

* Improved the selection of multiple instructions.

* Load/store ``lb``, ``sb``, ``ld``, ``sd``, ``lld``, ... instructions
  now support 32/64-bit offsets.

* Added support for ``y``, ``M``, and ``L`` inline assembler operand codes.

* Extended list of relocations supported by the ``.reloc`` directive

* Fixed using a wrong register class for creating an emergency
  spill slot for mips3 / n64 ABI.

* MIPS relocation types were generated for microMIPS code.

* Corrected definitions of multiple instructions (``lwp``, ``swp``, ``ctc2``,
  ``cfc2``, ``sync``, ``synci``, ``cvt.d.w``, ...).

* Fixed atomic operations at ``-O0`` level.

* Fixed local dynamic TLS with Sym64

Changes to the PowerPC Target
-----------------------------

During this release the PowerPC target has:

* Replaced the list scheduler for post register allocation with the machine scheduler.

* Machine model for AMD bdver2 (Piledriver) CPU was added. It is used to support
  instruction scheduling and other instruction cost heuristics.

* Added support for ``symbol@high`` and ``symbol@higha`` symbol modifiers.

* Added support for quad-precision floating point type (``__float128``) under the llvm option ``-enable-ppc-quad-precision``.

* Added dump function to ``LatencyPriorityQueue``.

* Completed the Power9 scheduler model.

* Optimized TLS code generation.

* Improved MachineLICM for hoisting constant stores.

* Improved code generation to reduce register use by using more register + immediate instructions.

* Improved code generation to better exploit rotate-and-mask instructions.

* Fixed the bug in dynamic loader for JIT which crashed NNVM.

* Numerous bug fixes and code cleanups.

Changes to the SystemZ Target
-----------------------------

During this release the SystemZ target has:

* Added support for vector registers in inline asm statements.

* Added support for stackmaps, patchpoints, and the anyregcc
  calling convention.

* Changed the default function alignment to 16 bytes.

* Improved codegen for condition code handling.

* Improved instruction scheduling and microarchitecture tuning for z13/z14.

* Fixed support for generating GCOV coverage data.

* Fixed some codegen bugs.

Changes to the X86 Target
-------------------------

* The calling convention for the ``f80`` data type on MinGW targets has been
  fixed. Normally, the calling convention for this type is handled within clang,
  but if an intrinsic is used, which LLVM expands into a libcall, the
  proper calling convention needs to be supported in LLVM as well. (Note,
  on Windows, this data type is only used for long doubles in MinGW
  environments - in MSVC environments, long doubles are the same size as
  normal doubles.)

Changes to the OCaml bindings
-----------------------------



Changes to the C API
--------------------


* Expanded the OrcJIT APIs so they can register event listeners like debuggers
  and profilers.

Changes to the DAG infrastructure
---------------------------------

External Open Source Projects Using LLVM 8
==========================================

Zig Programming Language
------------------------

`Zig <https://ziglang.org>`_  is an open-source programming language designed
for robustness, optimality, and clarity. Zig is an alternative to C, providing
high level features such as generics, compile time function execution, partial
evaluation, and LLVM-based coroutines, while exposing low level LLVM IR
features such as aliases and intrinsics. Zig uses Clang to provide automatic
import of .h symbols - even inline functions and macros. Zig uses LLD combined
with lazily building compiler-rt to provide out-of-the-box cross-compiling for
all supported targets.


Additional Information
======================

A wide variety of additional information is available on the `LLVM web page
<https://llvm.org/>`_, in particular in the `documentation
<https://llvm.org/docs/>`_ section.  The web page also contains versions of the
API documentation which is up-to-date with the Subversion version of the source
code.  You can access versions of these documents specific to this release by
going into the ``llvm/docs/`` directory in the LLVM tree.

If you have any questions or comments about LLVM, please feel free to contact
us via the `mailing lists <https://llvm.org/docs/#mailing-lists>`_.
