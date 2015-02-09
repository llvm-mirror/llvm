//===-- llvm-vtabledump.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_VTABLEDUMP_LLVM_VTABLEDUMP_H
#define LLVM_TOOLS_LLVM_VTABLEDUMP_LLVM_VTABLEDUMP_H

#include "llvm/Support/CommandLine.h"
#include <string>

namespace opts {
extern llvm::cl::list<std::string> InputFilenames;
} // namespace opts

#define LLVM_VTABLEDUMP_ENUM_ENT(ns, enum)                                     \
  { #enum, ns::enum }

#endif
