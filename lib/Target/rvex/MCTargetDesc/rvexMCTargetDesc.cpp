//===-- rvexMCTargetDesc.cpp - rvex Target Descriptions ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//                               rvex Backend
//
// Author: David Juhasz
// E-mail: juhda@caesar.elte.hu
// Institute: Dept. of Programming Languages and Compilers, ELTE IK, Hungary
//
// The research is supported by the European Union and co-financed by the
// European Social Fund (grant agreement no. TAMOP
// 4.2.1./B-09/1/KMR-2010-0003).
//
// This file provides rvex specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "rvexMCTargetDesc.h"
#include "rvexMCAsmInfo.h"
#include "llvm/MC/MCCodeGenInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/TargetRegistry.h"

#define GET_INSTRINFO_MC_DESC
#include "rvexGenInstrInfo.inc"

  namespace llvm {
  // Sorted (by key) array of values for CPU features.
  extern const llvm::SubtargetFeatureKV rvexFeatureKV[] = {
    { "vliw", "Enable VLIW scheduling", rvex::FeatureVLIW, 0ULL }
  };

  // Sorted (by key) array of values for CPU subtype.
  extern const llvm::SubtargetFeatureKV rvexSubTypeKV[] = {
    { "rvex", "Select the rvex processor", 0ULL, 0ULL },
    { "rvex-vliw", "Select the rvex-vliw processor", rvex::FeatureVLIW, 0ULL }
  };

  #ifdef DBGFIELD
  #error "<target>GenSubtargetInfo.inc requires a DBGFIELD macro"
  #endif
  #ifndef NDEBUG
  #define DBGFIELD(x) x,
  #else
  #define DBGFIELD(x)
  #endif
/*
  int rvexDFAStateInputTable[][2] = {
    {15, 1},    
    {15, 2},    
    {15, 3},    
    {15, 4},    
    {-1, -1},
    {-1, -1}
  };

  unsigned int rvexDFAStateEntryTable[] = {
    0, 1, 2, 3, 4, 5, 
  };*/

  int rvexDFAStateInputTable[][2] = {
    {1, 4},    {2, 2},    {3, 5},    {4, 3},    {7, 1},    
    {1, 10},    {2, 13},    {3, 6},    {4, 8},    {7, 6},    
    {1, 7},    {3, 7},    {4, 12},    {7, 13},    
    {1, 11},    {2, 12},    {3, 8},    {7, 8},    
    {2, 7},    {3, 7},    {4, 11},    {7, 10},    
    {1, 7},    {2, 7},    {3, 7},    {4, 8},    {7, 6},    
    {1, 9},    {2, 9},    {3, 9},    {4, 9},    {7, 9},    
    {4, 9},    {7, 9},    
    {1, 9},    {2, 9},    {3, 9},    {7, 9},    
    {-1, -1},
    {2, 9},    {3, 9},    {4, 9},    {7, 9},    
    {2, 9},    {3, 9},    {7, 9},    
    {1, 9},    {3, 9},    {7, 9},    
    {1, 9},    {3, 9},    {4, 9},    {7, 9},    
    {-1, -1}
  };

  unsigned int rvexDFAStateEntryTable[] = {
    0, 5, 10, 14, 18, 22, 27, 32, 34, 38, 39, 43, 46, 49, 53, 
  };

  // Functional units for "rvexGenericItineraries"
  namespace rvexGenericItinerariesFU {
    const unsigned P0 = 1 << 0;
    const unsigned P1 = 1 << 1;
    const unsigned P2 = 1 << 2;
  }

  llvm::InstrStage rvexStages[] = {
    { 0, 0, 0, llvm::InstrStage::Required }, // No itinerary
    { 1, rvexGenericItinerariesFU::P0 | rvexGenericItinerariesFU::P1, -1, (llvm::InstrStage::ReservationKinds)0 }, // 1
    { 1, rvexGenericItinerariesFU::P0 | rvexGenericItinerariesFU::P1 | rvexGenericItinerariesFU::P2, -1, (llvm::InstrStage::ReservationKinds)0 }, // 2
    { 1, rvexGenericItinerariesFU::P0, -1, (llvm::InstrStage::ReservationKinds)0 }, // 3
    { 1, rvexGenericItinerariesFU::P1, -1, (llvm::InstrStage::ReservationKinds)0 }, // 4
    { 1, rvexGenericItinerariesFU::P2, -1, (llvm::InstrStage::ReservationKinds)0 }, // 5
    { 0, 0, 0, llvm::InstrStage::Required } // End stages
  };

  namespace rvexGenericItinerariesFU2 {
    const unsigned P0 = 1 << 0;
    const unsigned P1 = 1 << 1;
    const unsigned P2 = 1 << 2;
    const unsigned P3 = 1 << 3;
  }

  extern const unsigned rvexOperandCycles[] = {
    0, // No itinerary
    0 // End operand cycles
  };
  extern const unsigned rvexForwardingPaths[] = {
   0, // No itinerary
   0 // End bypass tables
  };

  static const llvm::InstrItinerary *NoItineraries = 0;

  static llvm::InstrItinerary rvexGenericItineraries[] = {
    { 0, 0, 0, 0, 0 }, // 0 NoInstrModel
    { 1, 1, 2, 0, 0 }, // 1 IIAlu
    { 1, 2, 3, 0, 0 }, // 2 IIPseudo
    { 1, 3, 4, 0, 0 }, // 3 IIBranch
    { 1, 1, 2, 0, 0 }, // 4 IILoad
    { 1, 4, 5, 0, 0 }, // 5 IIHiLo
    { 1, 4, 5, 0, 0 }, // 6 IIImul
    { 1, 3, 4, 0, 0 }, // 7 IIStore
    { 1, 5, 6, 0, 0 }, // 8 IIIdiv
    { 0, ~0U, ~0U, ~0U, ~0U } // end marker
  };

  // ===============================================================
  // Data tables for the new per-operand machine model.

  // {ProcResourceIdx, Cycles}
  extern const llvm::MCWriteProcResEntry rvexWriteProcResTable[] = {
    { 0,  0}, // Invalid
  }; // rvexWriteProcResTable

  // {Cycles, WriteResourceID}
  extern const llvm::MCWriteLatencyEntry rvexWriteLatencyTable[] = {
    { 0,  0}, // Invalid
  }; // rvexWriteLatencyTable

  // {UseIdx, WriteResourceID, Cycles}
  extern const llvm::MCReadAdvanceEntry rvexReadAdvanceTable[] = {
    {0,  0,  0}, // Invalid
  }; // rvexReadAdvanceTable

  static const llvm::MCSchedModel NoSchedModel(
    MCSchedModel::DefaultIssueWidth,
    MCSchedModel::DefaultMinLatency,
    MCSchedModel::DefaultLoadLatency,
    MCSchedModel::DefaultHighLatency,
    MCSchedModel::DefaultILPWindow,
    MCSchedModel::DefaultMispredictPenalty,
    0, // Processor ID
    0, 0, 0, 0, // No instruction-level machine model.
    NoItineraries);

  static const llvm::MCSchedModel rvexModel(
    4, // IssueWidth
    MCSchedModel::DefaultMinLatency,
    MCSchedModel::DefaultLoadLatency,
    MCSchedModel::DefaultHighLatency,
    MCSchedModel::DefaultILPWindow,
    MCSchedModel::DefaultMispredictPenalty,
    1, // Processor ID
    0, 0, 0, 0, // No instruction-level machine model.
    rvexGenericItineraries);

  // Sorted (by key) array of itineraries for CPU subtype.
  extern const llvm::SubtargetInfoKV rvexProcSchedKV[] = {
    { "rvex", (const void *)&rvexModel },
    { "rvex-vliw", (const void *)&rvexModel }
  };
  #undef DBGFIELD
  static inline void InitrvexMCSubtargetInfo(MCSubtargetInfo *II, StringRef TT, StringRef CPU, StringRef FS) {
    II->InitMCSubtargetInfo(TT, CPU, FS, rvexFeatureKV, rvexSubTypeKV, 
                        rvexProcSchedKV, rvexWriteProcResTable, rvexWriteLatencyTable, rvexReadAdvanceTable, 
                        rvexStages, rvexOperandCycles, rvexForwardingPaths, 1, 2);
  }

} // End llvm namespace 

#define GET_REGINFO_MC_DESC
#include "rvexGenRegisterInfo.inc"

using namespace llvm;

static MCInstrInfo *creatervexMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitrvexMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *creatervexMCRegisterInfo(StringRef TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitrvexMCRegisterInfo(X, rvex::R0); //TODO: is that right?
  return X;
}

static MCSubtargetInfo *creatervexMCSubtargetInfo(StringRef TT, StringRef CPU,
                                                    StringRef FS) {
  MCSubtargetInfo *X = new MCSubtargetInfo();
  InitrvexMCSubtargetInfo(X, TT, CPU, FS);
  return X;
}

static MCCodeGenInfo *creatervexMCCodeGenInfo(StringRef TT, Reloc::Model RM,
                                                CodeModel::Model CM,
                                                CodeGenOpt::Level OL) {
  MCCodeGenInfo *X = new MCCodeGenInfo();
  X->InitMCCodeGenInfo(RM, CM, OL);
  return X;
}

extern "C" void LLVMInitializervexTargetMC() {
  // Register the MC asm info.
  //rvexStages = rvexStages2;



  llvm::InstrStage temp = {0, 0, 0, llvm::InstrStage::Required};
  llvm::InstrStage temp1 = { 1, rvexGenericItinerariesFU2::P0 | rvexGenericItinerariesFU2::P1 | rvexGenericItinerariesFU2::P2 | rvexGenericItinerariesFU2::P3, -1, (llvm::InstrStage::ReservationKinds)0 };
  llvm::InstrStage temp2 = {0, 0, 0, llvm::InstrStage::Required};
/*
  rvexStages[0] = temp;
  rvexStages[1] = temp1;
  rvexStages[2] = temp2;

  llvm::InstrItinerary itin_temp = { 0, 0, 0, 0, 0 }; // 0 NoInstrModel
  rvexGenericItineraries[0] = itin_temp;
  llvm::InstrItinerary itin_temp1 = { 1, 1, 2, 0, 0 }; // 0 NoInstrModel
  rvexGenericItineraries[1] = itin_temp1;
  llvm::InstrItinerary itin_temp2 = { 1, 1, 2, 0, 0 }; // 0 NoInstrModel
  rvexGenericItineraries[2] = itin_temp2;
  llvm::InstrItinerary itin_temp3 = { 1, 1, 2, 0, 0 }; // 0 NoInstrModel
  rvexGenericItineraries[3] = itin_temp3;
  llvm::InstrItinerary itin_temp4 = { 1, 1, 2, 0, 0 }; // 0 NoInstrModel
  rvexGenericItineraries[4] = itin_temp4;
  llvm::InstrItinerary itin_temp5 = { 1, 1, 2, 0, 0 }; // 0 NoInstrModel
  rvexGenericItineraries[5] = itin_temp5;
  llvm::InstrItinerary itin_temp6 = { 1, 1, 2, 0, 0 }; // 0 NoInstrModel
  rvexGenericItineraries[6] = itin_temp6;
  llvm::InstrItinerary itin_temp7 = { 1, 1, 2, 0, 0 }; // 0 NoInstrModel
  rvexGenericItineraries[7] = itin_temp7;
  llvm::InstrItinerary itin_temp8 = { 1, 1, 2, 0, 0 }; // 0 NoInstrModel
  rvexGenericItineraries[8] = itin_temp8;*/
    


  RegisterMCAsmInfo<rvexELFMCAsmInfo> X(ThervexTarget);

  // Register the MC codegen info.
  TargetRegistry::RegisterMCCodeGenInfo(ThervexTarget,
                                        creatervexMCCodeGenInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(ThervexTarget, creatervexMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(ThervexTarget, creatervexMCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(ThervexTarget,
                                          creatervexMCSubtargetInfo);
}

