(*===-- llvm_bitwriter.mli - LLVM OCaml Interface -------------*- OCaml -*-===*
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *
 *===----------------------------------------------------------------------===*)

(** Bitcode writer.

    This interface provides an OCaml API for the LLVM bitcode writer, the
    classes in the Bitwriter library. *)

(** [write_bitcode_file m path] writes the bitcode for module [m] to the file at
    [path]. Returns [true] if successful, [false] otherwise. *)
external write_bitcode_file
  : Llvm.llmodule -> string -> bool
  = "llvm_write_bitcode_file"

(** [write_bitcode_to_fd ~unbuffered fd m] writes the bitcode for module
    [m] to the channel [c]. If [unbuffered] is [true], after every write the fd
    will be flushed. Returns [true] if successful, [false] otherwise. *)
external write_bitcode_to_fd
  : ?unbuffered:bool -> Llvm.llmodule -> Unix.file_descr -> bool
  = "llvm_write_bitcode_to_fd"

(** [write_bitcode_to_memory_buffer m] returns a memory buffer containing
    the bitcode for module [m]. *)
external write_bitcode_to_memory_buffer
  : Llvm.llmodule -> Llvm.llmemorybuffer
  = "llvm_write_bitcode_to_memory_buffer"

(** [output_bitcode ~unbuffered c m] writes the bitcode for module [m]
    to the channel [c]. If [unbuffered] is [true], after every write the fd
    will be flushed. Returns [true] if successful, [false] otherwise. *)
val output_bitcode : ?unbuffered:bool -> out_channel -> Llvm.llmodule -> bool
