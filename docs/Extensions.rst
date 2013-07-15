===============
LLVM Extensions
===============

.. contents::
   :local:

.. toctree::
   :hidden:

Introduction
============

This document describes extensions to tools and formats LLVM seeks compatibility
with.

Machine-specific Assembly Syntax
================================

X86/COFF-Dependent
------------------

Relocations
^^^^^^^^^^^

The following additional relocation type is supported:

**@IMGREL** (AT&T syntax only) generates an image-relative relocation that
corresponds to the COFF relocation types ``IMAGE_REL_I386_DIR32NB`` (32-bit) or
``IMAGE_REL_AMD64_ADDR32NB`` (64-bit).

.. code-block:: gas

  .text
  fun:
    mov foo@IMGREL(%ebx, %ecx, 4), %eax

  .section .pdata
    .long fun@IMGREL
    .long (fun@imgrel + 0x3F)
    .long $unwind$fun@imgrel


``.linkonce`` Directive
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Syntax:

   ``.linkonce [ comdat type [ section identifier ] ]``

Supported COMDAT types:

``discard``
   Discards duplicate sections with the same COMDAT symbol. This is the default
   if no type is specified.

``one_only``
   If the symbol is defined multiple times, the linker issues an error.

``same_size``
   Duplicates are discarded, but the linker issues an error if any have
   different sizes.

``same_contents``
   Duplicates are discarded, but the linker issues an error if any duplicates
   do not have exactly the same content.

``associative``
   Links the section if a certain other COMDAT section is linked. This other
   section is indicated by its section identifier following the comdat type.
   The following restrictions apply to the associated section:

   1. It must be the name of a section already defined.
   2. It must differ from the current section.
   3. It must be a COMDAT section.
   4. It cannot be another associative COMDAT section.

``largest``
   Links the largest section from among the duplicates.

``newest``
   Links the newest section from among the duplicates.


.. code-block:: gas

  .section .text$foo
  .linkonce
    ...

  .section .xdata$foo
  .linkonce associative .text$foo
    ...
