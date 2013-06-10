//===-- rvexSelectionDAGInfo.cpp - rvex SelectionDAG Info -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the rvexSelectionDAGInfo class.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "rvex-selectiondag-info"
#include "rvexTargetMachine.h"
using namespace llvm;

rvexSelectionDAGInfo::rvexSelectionDAGInfo(const rvexTargetMachine &TM)
  : TargetSelectionDAGInfo(TM) {
}

rvexSelectionDAGInfo::~rvexSelectionDAGInfo() {
}
