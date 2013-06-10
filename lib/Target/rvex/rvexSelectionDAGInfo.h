//===-- rvexSelectionDAGInfo.h - rvex SelectionDAG Info ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the rvex subclass for TargetSelectionDAGInfo.
//
//===----------------------------------------------------------------------===//

#ifndef rvexSELECTIONDAGINFO_H
#define rvexSELECTIONDAGINFO_H

#include "llvm/Target/TargetSelectionDAGInfo.h"

namespace llvm {

class rvexTargetMachine;

class rvexSelectionDAGInfo : public TargetSelectionDAGInfo {
public:
  explicit rvexSelectionDAGInfo(const rvexTargetMachine &TM);
  ~rvexSelectionDAGInfo();
};

}

#endif
