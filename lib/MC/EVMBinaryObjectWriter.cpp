//===- lib/MC/EVMBinaryObjectWriter.cpp - EVMBinary File Writer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements EVMBinary object file writer information.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/BinaryFormat/EVM.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCEVMObjectWriter.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/StringSaver.h"
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "mc"

namespace {
class EVMBinaryObjectWriter : public MCObjectWriter {
public:
  EVMBinaryObjectWriter(std::unique_ptr<MCEVMObjectTargetWriter> MOTW,
                   raw_pwrite_stream &OS) : W(OS, support::big) {}

private:
  void recordRelocation(MCAssembler &Asm, const MCAsmLayout &Layout,
                        const MCFragment *Fragment, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override;

  void executePostLayoutBinding(MCAssembler &Asm,
                                const MCAsmLayout &Layout) override;

  uint64_t writeObject(MCAssembler &Asm, const MCAsmLayout &Layout) override;

  support::endian::Writer W;
};

} // end anonymous namespace


void EVMBinaryObjectWriter::executePostLayoutBinding(MCAssembler &Asm,
                                                const MCAsmLayout &Layout) {
  llvm_unreachable("unimplemented");
}

void EVMBinaryObjectWriter::recordRelocation(MCAssembler &Asm,
                                        const MCAsmLayout &Layout,
                                        const MCFragment *Fragment,
                                        const MCFixup &Fixup, MCValue Target,
                                        uint64_t &FixedValue) {
  MCAsmBackend &Backend = Asm.getBackend();
  bool IsPCRel = Backend.getFixupKindInfo(Fixup.getKind()).Flags &
                 MCFixupKindInfo::FKF_IsPCRel;
  if (IsPCRel){
    llvm_unreachable("unimplemented");
  }

}

uint64_t EVMBinaryObjectWriter::writeObject(MCAssembler &Asm,
                                       const MCAsmLayout &Layout) {
  uint64_t StartOffset = 0;



  llvm_unreachable("unimplemented");
}

std::unique_ptr<MCObjectWriter>
llvm::createEVMObjectWriter(std::unique_ptr<MCEVMObjectTargetWriter> MOTW,
                            raw_pwrite_stream &OS) {
  return llvm::make_unique<EVMBinaryObjectWriter>(std::move(MOTW), OS);
}
