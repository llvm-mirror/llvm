/*===-- llvm-c/Support.h - C Interface Types declarations ---------*- C -*-===*\
|*                                                                            *|
|*                     The LLVM Compiler Infrastructure                       *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file defines types used by the the C interface to LLVM.               *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_TYPES_H
#define LLVM_C_TYPES_H

#include "llvm/Support/DataTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup LLVMCSupportTypes Types and Enumerations
 *
 * @{
 */

typedef int LLVMBool;

/* Opaque types. */

/**
 * LLVM uses a polymorphic type hierarchy which C cannot represent, therefore
 * parameters must be passed as base types. Despite the declared types, most
 * of the functions provided operate only on branches of the type hierarchy.
 * The declared parameter names are descriptive and specify which type is
 * required. Additionally, each type hierarchy is documented along with the
 * functions that operate upon it. For more detail, refer to LLVM's C++ code.
 * If in doubt, refer to Core.cpp, which performs parameter downcasts in the
 * form unwrap<RequiredType>(Param).
 */

/**
 * Used to pass regions of memory through LLVM interfaces.
 *
 * @see llvm::MemoryBuffer
 */
typedef struct LLVMOpaqueMemoryBuffer *LLVMMemoryBufferRef;

/**
 * The top-level container for all LLVM global data. See the LLVMContext class.
 */
typedef struct LLVMOpaqueContext *LLVMContextRef;

/**
 * The top-level container for all other LLVM Intermediate Representation (IR)
 * objects.
 *
 * @see llvm::Module
 */
typedef struct LLVMOpaqueModule *LLVMModuleRef;

/**
 * Each value in the LLVM IR has a type, an LLVMTypeRef.
 *
 * @see llvm::Type
 */
typedef struct LLVMOpaqueType *LLVMTypeRef;

/**
 * Represents an individual value in LLVM IR.
 *
 * This models llvm::Value.
 */
typedef struct LLVMOpaqueValue *LLVMValueRef;

/**
 * Represents a basic block of instructions in LLVM IR.
 *
 * This models llvm::BasicBlock.
 */
typedef struct LLVMOpaqueBasicBlock *LLVMBasicBlockRef;

/**
 * Represents an LLVM basic block builder.
 *
 * This models llvm::IRBuilder.
 */
typedef struct LLVMOpaqueBuilder *LLVMBuilderRef;

/**
 * Interface used to provide a module to JIT or interpreter.
 * This is now just a synonym for llvm::Module, but we have to keep using the
 * different type to keep binary compatibility.
 */
typedef struct LLVMOpaqueModuleProvider *LLVMModuleProviderRef;

/** @see llvm::PassManagerBase */
typedef struct LLVMOpaquePassManager *LLVMPassManagerRef;

/** @see llvm::PassRegistry */
typedef struct LLVMOpaquePassRegistry *LLVMPassRegistryRef;

/**
 * Used to get the users and usees of a Value.
 *
 * @see llvm::Use */
typedef struct LLVMOpaqueUse *LLVMUseRef;

/**
 * @see llvm::DiagnosticInfo
 */
typedef struct LLVMOpaqueDiagnosticInfo *LLVMDiagnosticInfoRef;

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif
