//===-- MBlazeIntrinsicInfo.cpp - Intrinsic Information -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the MBlaze implementation of TargetIntrinsicInfo.
//
//===----------------------------------------------------------------------===//

#include "MBlazeIntrinsicInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>

using namespace llvm;

namespace mblazeIntrinsic {

  enum ID {
    last_non_mblaze_intrinsic = Intrinsic::num_intrinsics-1,
#define GET_INTRINSIC_ENUM_VALUES
#include "MBlazeGenIntrinsics.inc"
#undef GET_INTRINSIC_ENUM_VALUES
    , num_mblaze_intrinsics
  };

#define GET_LLVM_INTRINSIC_FOR_GCC_BUILTIN
#include "MBlazeGenIntrinsics.inc"
#undef GET_LLVM_INTRINSIC_FOR_GCC_BUILTIN
}

std::string MBlazeIntrinsicInfo::getName(unsigned IntrID, Type **Tys,
                                         unsigned numTys) const {
  static const char *const names[] = {
#define GET_INTRINSIC_NAME_TABLE
#include "MBlazeGenIntrinsics.inc"
#undef GET_INTRINSIC_NAME_TABLE
  };

  assert(!isOverloaded(IntrID) && "MBlaze intrinsics are not overloaded");
  if (IntrID < Intrinsic::num_intrinsics)
    return 0;
  assert(IntrID < mblazeIntrinsic::num_mblaze_intrinsics &&
         "Invalid intrinsic ID");

  std::string Result(names[IntrID - Intrinsic::num_intrinsics]);
  return Result;
}

unsigned MBlazeIntrinsicInfo::
lookupName(const char *Name, unsigned Len) const {
  if (!StringRef(Name, Len).startswith("llvm."))
    return 0; // All intrinsics start with 'llvm.'

#define GET_FUNCTION_RECOGNIZER
#include "MBlazeGenIntrinsics.inc"
#undef GET_FUNCTION_RECOGNIZER
  return 0;
}

unsigned MBlazeIntrinsicInfo::
lookupGCCName(const char *Name) const {
    return mblazeIntrinsic::getIntrinsicForGCCBuiltin("mblaze",Name);
}

bool MBlazeIntrinsicInfo::isOverloaded(unsigned IntrID) const {
  if (IntrID == 0)
    return false;

  unsigned id = IntrID - Intrinsic::num_intrinsics + 1;
#define GET_INTRINSIC_OVERLOAD_TABLE
#include "MBlazeGenIntrinsics.inc"
#undef GET_INTRINSIC_OVERLOAD_TABLE
}

/// This defines the "getAttributes(LLVMContext &C, ID id)" method.
#define GET_INTRINSIC_ATTRIBUTES
#include "MBlazeGenIntrinsics.inc"
#undef GET_INTRINSIC_ATTRIBUTES

static FunctionType *getType(LLVMContext &Context, unsigned id) {
  Type *ResultTy = NULL;
  SmallVector<Type*, 8> ArgTys;
  bool IsVarArg = false;

#define GET_INTRINSIC_GENERATOR
#include "MBlazeGenIntrinsics.inc"
#undef GET_INTRINSIC_GENERATOR

  return FunctionType::get(ResultTy, ArgTys, IsVarArg);
}

Function *MBlazeIntrinsicInfo::getDeclaration(Module *M, unsigned IntrID,
                                                Type **Tys,
                                                unsigned numTy) const {
  assert(!isOverloaded(IntrID) && "MBlaze intrinsics are not overloaded");
  AttributeSet AList = getAttributes(M->getContext(),
                                    (mblazeIntrinsic::ID) IntrID);
  return cast<Function>(M->getOrInsertFunction(getName(IntrID),
                                               getType(M->getContext(), IntrID),
                                               AList));
}
