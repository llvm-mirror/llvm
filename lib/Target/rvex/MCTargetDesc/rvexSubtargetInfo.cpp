#include "rvexSubtargetInfo.h"

#include "rvexBuildDFA.h"
#include "rvexReadConfig.h"

#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/CommandLine.h"

#define GET_SUBTARGETINFO_ENUM
#include "rvexGenSubtargetInfo.inc"

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

  // rVex stages, initialized with default values
  static std::vector<llvm::InstrStage> rvexStages {
    { 0, 0, 0, llvm::InstrStage::Required }, // No itinerary
    { 1, 255, 1, llvm::InstrStage::Required }, // 255
    { 2, 255, 1, llvm::InstrStage::Required }, // 255
    { 2,   1, 1, llvm::InstrStage::Required }, // 1
    { 2,  63, 1, llvm::InstrStage::Required }, // 63
    { 0, 0, 0, llvm::InstrStage::Required } // End stages
  };

  extern const unsigned rvexOperandCycles[] = {
    0, // No itinerary
    0 // End operand cycles
  };
  extern const unsigned rvexForwardingPaths[] = {
   0, // No itinerary
   0 // End bypass tables
  };

  // rVex Itineraries, initialized with default values
  static std::vector<llvm::InstrItinerary> rvexGenericItineraries {
    { 0, 0, 0, 0, 0 }, // 0 NoInstrModel
    { 1, 1, 1, 0, 0 }, // 1 IIAlu
    { 1, 3, 3, 0, 0 }, // 2 IILoadStore
    { 1, 2, 2, 0, 0 }, // 3 IIBranch
    { 1, 1, 1, 0, 0 }, // 4 IIHiLo
    { 1, 4, 4, 0, 0 }, // 5 IIImul
    { 1, 1, 1, 0, 0 }, // 6 IIAluImm
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

  static llvm::MCSchedModel rvexModel = {
    8, // IssueWidth
    MCSchedModel::DefaultMicroOpBufferSize,
    MCSchedModel::DefaultLoopMicroOpBufferSize,
    2, // LoadLatency
    MCSchedModel::DefaultHighLatency,
    MCSchedModel::DefaultMispredictPenalty,
    false,
    true,
    1, // Processor ID
    nullptr, nullptr, 0, 0, // No instruction-level machine model.
    nullptr // Will become a pointer to rvexGenericItineraries.data()
  };

  // Sorted (by key) array of itineraries for CPU subtype.
  extern const llvm::SubtargetInfoKV rvexProcSchedKV[] = {
    { "rvex", (const void *)&rvexModel },
    { "rvex-vliw", (const void *)&rvexModel }
  };

  void InitrvexMCSubtargetInfo(MCSubtargetInfo *II, StringRef TT, StringRef CPU, StringRef FS) {
    rvexModel.InstrItineraries = rvexGenericItineraries.data();
    II->InitMCSubtargetInfo(TT, CPU, FS, rvexFeatureKV, rvexSubTypeKV, 
                        rvexProcSchedKV, rvexWriteProcResTable, rvexWriteLatencyTable, rvexReadAdvanceTable, 
                        rvexStages.data(), rvexOperandCycles, rvexForwardingPaths);
  }

  namespace {
    cl::opt<std::string> ConfigFile("config", cl::desc("Path to config file"));

    bool Is_Generic_flag = false;
  }

  bool rvexIsGeneric() {
    return Is_Generic_flag;
  }

  void rvexUpdateSubtargetInfoFromConfig() {
    // Read configuration file
    auto config = read_config(ConfigFile);

    //Init InstrStages from config file
    std::vector<Stage_desc> const *stages = std::get<0>(config);
    std::vector<DFAState> const *itineraries = std::get<1>(config);
    Is_Generic_flag = std::get<2>(config);
    rvexModel.IssueWidth = std::get<3>(config);

    // Remove everything except the first and last element from rvexStages
    //XXX: only do this if stages is not empty?
    if(!stages->empty()) {
        rvexStages.erase(rvexStages.begin()+1, rvexStages.end()-1);

        for_each(stages->cbegin(), stages->cend(), [](Stage_desc const &s) {
                llvm::InstrStage tempStage = {s.delay, s.FU, -1, llvm::InstrStage::Required};
                // insert new stage before last one
                rvexStages.insert(rvexStages.end()-1, tempStage);
                });
    }

    if(!itineraries->empty()) {
        rvexGenericItineraries.erase(rvexGenericItineraries.begin()+1, rvexGenericItineraries.end()-1);
        // Init InstrItin from config file
        for_each(itineraries->cbegin(), itineraries->cend(), [](DFAState const &i) {
                llvm::InstrItinerary tempItin = {0, i.num1, i.num2, 0, 0};
                rvexGenericItineraries.insert(rvexGenericItineraries.end() - 1, tempItin);
                });
    }

    // Build VLIW DFA from config stages
    rvexBuildDFA(*stages);
  }

} // End llvm namespace 

