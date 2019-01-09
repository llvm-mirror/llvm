#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BreadthFirstIterator.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Format.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/LoopVersioning.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"

#include <cstdlib>
#include <sstream>
#include <fstream>


using namespace llvm;

#define DEBUG_TYPE "offload"

namespace {

Instruction* DSUGetRepresentitive(
  std::map<Instruction*, Instruction*> &DSU,
  Instruction *Inst
) {
  assert(DSU.find(Inst) != DSU.end());
  return DSU[Inst] == Inst ? Inst : DSU[Inst] = DSUGetRepresentitive(DSU, DSU[Inst]);
}

std::string getInstructionOperand(
  const std::map<Instruction*, int> &Table,
  Value *Val
) {
  if (auto Inst = dyn_cast<Instruction>(Val)) {
    auto Iter = Table.find(Inst);
    assert(Iter != Table.end());
    return "v" + std::to_string(Iter->second);
  } else if (auto Const = dyn_cast<Constant>(Val)) {
    //FIXME: I do not know how to do Constant yet, write a POC!
    return Const->getName();
  } else {
    assert(0 && "Besides Inst and Const, not supported yet!");
  }
}

std::string getOperationStr(Type *Ty, const std::string &Name) {

  int Bitwidth = -1;

  switch (Ty->getTypeID()) {
  case Type::FloatTyID:
    Bitwidth = 32;
    break;
  case Type::DoubleTyID:
    Bitwidth = 64;
    break;
  case Type::IntegerTyID:
    Bitwidth = cast<IntegerType>(Ty)->getBitWidth();
    break;
  default:
    assert(0 && "Not supported!");
  }

  auto OpStr(Name);

  for (int i = 0, e = OpStr[0] == 'f' ? 2 : 1; i < e; ++i) {
    OpStr[i] -= 'a';
    OpStr[i] += 'A';
  }

  return OpStr + std::to_string(Bitwidth);
}


/// The information of a schedule we can extract from a .dfg.h file
struct ScheduleInfo {
  /// Key: v_x's x in the generated .dfg file
  /// Value: the corresponding port in this schedule
  std::map<int, int> IOPort;
  /// The size of the configuration array
  int ConfigSize;
  /// The values in the configuration array
  std::string ConfigString;

  ScheduleInfo() {}

  ScheduleInfo(const std::map<int, int> &IOPort, int ConfigSize, const std::string ConfigString) :
    IOPort(IOPort), ConfigSize(ConfigSize), ConfigString(ConfigString) {}
};

/// Parse the generated .dfg.h file, the filename should be xxx.dfg
ScheduleInfo ParseSchedule(const std::string &FileName) {
  std::map<int, int> IOPort;
  int ConfigSize = 0;
  std::string ConfigString;

  std::ifstream ifs(FileName + ".h");

  std::string Stripped(FileName);
  while (Stripped.back() != '.') {
    Stripped.pop_back();
  }
  Stripped.pop_back();

  std::string Line;
  std::string PortPrefix("P_" + Stripped + "_");
  while (std::getline(ifs, Line)) {
    std::istringstream iss(Line);
    std::string Token;
    iss >> Token;
    // #define xxx
    if (Token == "#define") {
      iss >> Token;
      // #define P_dfgx_vy
      if (Token.find(PortPrefix) == 0) {
        int IO = atoi(Token.substr(PortPrefix.size() + 1, Token.size()).c_str());
        int Port;
        iss >> Port;
        IOPort[IO] = Port;
        llvm::outs() << "v" << IO << " -> " << Port << "\n";
      // #define dfgx_size size
      } else if (Token.find(Stripped + "_") == 0) {
        iss >> ConfigSize;
      }
    // char dfgx_config[size] = "filename:dfgx.sched";
    } else if (Token == "char") {
      // dfgx_config[size]
      iss >> Token;
      // =
      iss >> Token;
      // "filename:dfgx.sched";
      iss >> Token;
      ConfigString = Token.substr(1, Token.size() - 3);
      while ((int)ConfigString.size() < ConfigSize)
        ConfigString.push_back('\0');
    }
  }

  llvm::outs() << "[Config] " << ConfigSize << ": " << ConfigString << "\n";
  return ScheduleInfo(IOPort, ConfigSize, ConfigString);
}


/// Emit DFG to .dfg text format and schedule it.
/// Factor: The unroll factor of the loop body.
/// ID: The ID of the DFG to be scheduled.
/// DFG: The instructions in the DFG.
/// Return the map from instruction to the value id.
std::map<Instruction*, int> ScheduleDFG(int Factor, int ID, const std::vector<Instruction*> &DFG) {

  std::error_code EC;
  std::string FileName = "dfg" + std::to_string(ID) + ".dfg";
  raw_fd_ostream TempDFG(FileName, EC);

  std::vector<bool>(DFG.size(), false);
  std::map<Instruction*, int> Alias;

  for (auto &Inst : DFG) {
    if (auto Load = dyn_cast<LoadInst>(Inst)) {
      Alias[Load] = Alias.size();
      TempDFG << "Input: v" << Alias[Load] << "\n";
    }
  }

  for (auto &Inst : DFG) {
    if (Inst->isBinaryOp()) {
      Alias[Inst] = Alias.size();

      std::string OpStr = getOperationStr(Inst->getType(), Inst->getOpcodeName());
      auto Op0 = getInstructionOperand(Alias, Inst->getOperand(0));
      auto Op1 = getInstructionOperand(Alias, Inst->getOperand(1));

      TempDFG << "v" << Alias[Inst] << " = " << OpStr << "(" << Op0 << ", " << Op1 << ")\n";
    }
  }

  int StoreCnt = 0;
  for (auto &Inst : DFG) {
    if (auto Store = dyn_cast<StoreInst>(Inst)) {
      auto Val = dyn_cast<Instruction>(Store->getValueOperand());
      assert(Alias.find(Val) != Alias.end());
      TempDFG << "Output: v" << Alias[Val] << "\n";
      ++StoreCnt;
    }
  }
  assert(Alias.size() + StoreCnt == DFG.size() && "Any ignored instructions?");

  TempDFG.close();

  llvm::outs() << "========== Generated DFG" << ID << " Textformat ==========\n";
  system(("cat " + FileName).c_str());
  llvm::outs() << "\n";
  // TODO: Support different fifo depth for DSE.
  assert(getenv("SSCONFIG") && "Please specify $SSCONFIG to schedule the DFG!");
  // Generate the schedule command.
  std::ostringstream oss;
  oss << "ss_sched --verbose -a sa ";
  oss << getenv("SSCONFIG");
  oss << " " << FileName;
  if (system(oss.str().c_str()) == 0) {
    llvm::outs() << "\nSuccessfully scheduled! Parse the generated file...\n";
  }
}


/// Offload DFG and analyze data flow access pattern
bool AnalyzeLoopAndOffloadDFGs(
  ScalarEvolution *SE,
  Loop *Current,
  int Factor
) {
  // Get the loop nest
  SmallVector<Loop*, 0> Loops;
  while (true) {
    Loops.push_back(Current);
    if (GetUnrollMetadata(Current->getLoopID(), "llvm.loop.ss.stream")) {
      break;
    }
    assert(Current->getParentLoop());
    Current = Current->getParentLoop();
    assert(Current && "A 'stream end' level is required!");
  }
  // Analyze the scalar-loop evolution
  auto Blocks = Loops[0]->getBlocks();

  std::map<Instruction*, Instruction*> DSU;

  for (auto Block : Blocks) {
    for (auto &Inst : *Block) {

      if (!LoadInst::classof(&Inst) && DSU.find(&Inst) == DSU.end()) 
        continue;

      if (LoadInst::classof(&Inst)) {
        DSU[&Inst] = &Inst;
      }

      assert(Instruction::classof(&Inst));
      for (auto user: (dyn_cast<Instruction>(&Inst))->users()) {
        if (!Instruction::classof(user))
          continue;

        auto UserInst = dyn_cast<Instruction>(user);
        if (DSU.find(UserInst) == DSU.end())
          DSU[UserInst] = UserInst;
        DSU[DSUGetRepresentitive(DSU, UserInst)] = DSUGetRepresentitive(DSU, &Inst);
      }

    }

  }

  static int TotalDfgs = 0;

  for (auto Block : Blocks) {
    for (auto &Outer : *Block) {

      if (DSU.find(&Outer) == DSU.end())
        continue;

      std::vector<Instruction*> DFG;
      /// Get the instructions in the same disjoint union
      auto Master = DSUGetRepresentitive(DSU, &Outer);
      for (auto &Inner : *Block)
        if (DSU.find(&Inner) != DSU.end() && DSUGetRepresentitive(DSU, &Inner) == Master)
          DFG.push_back(&Inner);

      /// Dump the DFG for debugging
      llvm::outs() << "========== Extracted DFG" << TotalDfgs << "==========\n";
      for (auto &Inst : DFG) {
        Inst->dump();
        DSU.erase(Inst);
      }
      llvm::outs() << "\n";

      ScheduleDFG(Factor, TotalDfgs, DFG);
      ++TotalDfgs;
    }
  }


  return true;
}

}

namespace {

struct StreamSpecialize : public FunctionPass {

  static char ID;
  StreamSpecialize() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    auto *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    auto *SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();

    for (auto Loop : *LI) {
      MDNode *MD = GetUnrollMetadata(Loop->getLoopID(), "llvm.loop.ss.dedicated");
      if (MD) {
        assert(MD->getNumOperands() == 2);
        auto MDFactor = dyn_cast<ConstantAsMetadata>(MD->getOperand(1));
        assert(MDFactor);
        int Factor =(int) MDFactor->getValue()->getUniqueInteger().getSExtValue();
        AnalyzeLoopAndOffloadDFGs(SE, Loop, Factor);
      }
    }
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<BlockFrequencyInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<LoopAccessLegacyAnalysis>();
    AU.addRequired<DemandedBitsWrapperPass>();
    AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<BasicAAWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
  }

};

struct POCAssemble : public FunctionPass {

  static char ID;
  POCAssemble() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    for (auto &bb: F) {
      for (auto &inst: bb) {
        if (auto Call = dyn_cast<CallInst>(&inst)) {
          if (Call->isInlineAsm()) {
            Call->dump();
            auto Asm = dyn_cast<InlineAsm>(Call->getCalledOperand());
            llvm::outs() << "Asm: "; Asm->dump();
            for (int i = 0, e = Call->getNumOperands(); i < e; ++i) {
              llvm::outs() << i << ": "; Call->getOperand(i)->dump();
            }
            llvm::outs() << "PtrType: "; Asm->getType()->dump();
            llvm::outs() << "FuncType: "; Asm->getFunctionType()->dump();
            llvm::outs() << Asm->getAsmString() << "\n"
                         << Asm->getConstraintString() << "\n\n";
          }
        }
      }
    }
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<BlockFrequencyInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<LoopAccessLegacyAnalysis>();
    AU.addRequired<DemandedBitsWrapperPass>();
    AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<BasicAAWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
  }

};

}

char StreamSpecialize::ID = 0;
char POCAssemble::ID = 1;
static RegisterPass<::StreamSpecialize> X("stream-specialize", "Stream specialize the program...", false, true);
static RegisterPass<::POCAssemble> Y("poc-assemble", "Assemble Call POC", false, true);
