/*===-- llvm-c/Linker.h - Module Linker C Interface -------------*- C++ -*-===*\
|*                                                                            *|
|*                     The LLVM Compiler Infrastructure                       *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file defines the C interface to the module/file/archive linker.       *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_LINKER_H
#define LLVM_C_LINKER_H

#include "llvm-c/Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* This enum is provided for backwards-compatibility only. It has no effect. */
typedef enum {
  LLVMLinkerDestroySource = 0, /* This is the default behavior. */
  LLVMLinkerPreserveSource_Removed = 1 /* This option has been deprecated and
                                          should not be used. */
} LLVMLinkerMode;

/* Links the source module into the destination module. The source module is
 * damaged. The only thing that can be done is destroy it. Optionally returns a
 * human-readable description of any errors that occurred in linking. OutMessage
 * must be disposed with LLVMDisposeMessage. The return value is true if an
 * error occurred, false otherwise.
 *
 * Note that the linker mode parameter \p Unused is no longer used, and has
 * no effect.
 *
 * This function is deprecated. Use LLVMLinkModules2 instead.
 */
LLVMBool LLVMLinkModules(LLVMModuleRef Dest, LLVMModuleRef Src,
                         LLVMLinkerMode Unused, char **OutMessage);

/* Links the source module into the destination module. The source module is
 * destroyed.
 * The return value is true if an error occurred, false otherwise.
 * Use the diagnostic handler to get any diagnostic message.
*/
LLVMBool LLVMLinkModules2(LLVMModuleRef Dest, LLVMModuleRef Src);

#ifdef __cplusplus
}
#endif

#endif
