//===-- PPC32CodeEmitter.cpp - JIT Code Emitter for PowerPC32 -----*- C++ -*-=//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
// 
// This file defines the PowerPC 32-bit CodeEmitter and associated machinery to
// JIT-compile bytecode to native PowerPC.
//
//===----------------------------------------------------------------------===//

#include "PPC32JITInfo.h"
#include "PPC32TargetMachine.h"
#include "PowerPC.h"
#include "llvm/Module.h"
#include "llvm/CodeGen/MachineCodeEmitter.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

namespace {
  class JITResolver {
    MachineCodeEmitter &MCE;

    // LazyCodeGenMap - Keep track of call sites for functions that are to be
    // lazily resolved.
    std::map<unsigned, Function*> LazyCodeGenMap;

    // LazyResolverMap - Keep track of the lazy resolver created for a
    // particular function so that we can reuse them if necessary.
    std::map<Function*, unsigned> LazyResolverMap;

  public:
    JITResolver(MachineCodeEmitter &mce) : MCE(mce) {}
    unsigned getLazyResolver(Function *F);
    unsigned addFunctionReference(unsigned Address, Function *F);
    
  private:
    unsigned emitStubForFunction(Function *F);
    static void CompilationCallback();
    unsigned resolveFunctionReference(unsigned RetAddr);
  };

  static JITResolver &getResolver(MachineCodeEmitter &MCE) {
    static JITResolver *TheJITResolver = 0;
    if (TheJITResolver == 0)
      TheJITResolver = new JITResolver(MCE);
    return *TheJITResolver;
  }
}

unsigned JITResolver::getLazyResolver(Function *F) {
  std::map<Function*, unsigned>::iterator I = LazyResolverMap.lower_bound(F);
  if (I != LazyResolverMap.end() && I->first == F) return I->second;

  unsigned Stub = emitStubForFunction(F);
  LazyResolverMap.insert(I, std::make_pair(F, Stub));
  return Stub;
}

/// addFunctionReference - This method is called when we need to emit the
/// address of a function that has not yet been emitted, so we don't know the
/// address.  Instead, we emit a call to the CompilationCallback method, and
/// keep track of where we are.
///
unsigned JITResolver::addFunctionReference(unsigned Address, Function *F) {
  LazyCodeGenMap[Address] = F;
  return (intptr_t)&JITResolver::CompilationCallback;
}

unsigned JITResolver::resolveFunctionReference(unsigned RetAddr) {
  std::map<unsigned, Function*>::iterator I = LazyCodeGenMap.find(RetAddr);
  assert(I != LazyCodeGenMap.end() && "Not in map!");
  Function *F = I->second;
  LazyCodeGenMap.erase(I);
  return MCE.forceCompilationOf(F);
}

/// emitStubForFunction - This method is used by the JIT when it needs to emit
/// the address of a function for a function whose code has not yet been
/// generated.  In order to do this, it generates a stub which jumps to the lazy
/// function compiler, which will eventually get fixed to call the function
/// directly.
///
unsigned JITResolver::emitStubForFunction(Function *F) {
  std::cerr << "PPC32CodeEmitter::emitStubForFunction() unimplemented!\n";
  abort();
  return 0;
}

void JITResolver::CompilationCallback() {
  std::cerr << "PPC32CodeEmitter: CompilationCallback() unimplemented!";
  abort();
}

namespace {
  class PPC32CodeEmitter : public MachineFunctionPass {
    TargetMachine &TM;
    MachineCodeEmitter &MCE;

    // Tracks which instruction references which BasicBlock
    std::vector<std::pair<const BasicBlock*,
                          std::pair<unsigned*,MachineInstr*> > > BBRefs;
    // Tracks where each BasicBlock starts
    std::map<const BasicBlock*, long> BBLocations;

    /// getMachineOpValue - evaluates the MachineOperand of a given MachineInstr
    ///
    int64_t getMachineOpValue(MachineInstr &MI, MachineOperand &MO);

    unsigned getAddressOfExternalFunction(Function *F);

  public:
    PPC32CodeEmitter(TargetMachine &T, MachineCodeEmitter &M) 
      : TM(T), MCE(M) {}

    const char *getPassName() const { return "PowerPC Machine Code Emitter"; }

    /// runOnMachineFunction - emits the given MachineFunction to memory
    ///
    bool runOnMachineFunction(MachineFunction &MF);

    /// emitBasicBlock - emits the given MachineBasicBlock to memory
    ///
    void emitBasicBlock(MachineBasicBlock &MBB);

    /// emitWord - write a 32-bit word to memory at the current PC
    ///
    void emitWord(unsigned w) { MCE.emitWord(w); }
    
    /// getValueBit - return the particular bit of Val
    ///
    unsigned getValueBit(int64_t Val, unsigned bit) { return (Val >> bit) & 1; }

    /// getBinaryCodeForInstr - This function, generated by the
    /// CodeEmitterGenerator using TableGen, produces the binary encoding for
    /// machine instructions.
    ///
    unsigned getBinaryCodeForInstr(MachineInstr &MI);
  };
}

/// addPassesToEmitMachineCode - Add passes to the specified pass manager to get
/// machine code emitted.  This uses a MachineCodeEmitter object to handle
/// actually outputting the machine code and resolving things like the address
/// of functions.  This method should returns true if machine code emission is
/// not supported.
///
bool PPC32TargetMachine::addPassesToEmitMachineCode(FunctionPassManager &PM,
                                                    MachineCodeEmitter &MCE) {
  // Keep as `true' until this is a functional JIT to allow llvm-gcc to build
  return true;

  // Machine code emitter pass for PowerPC
  PM.add(new PPC32CodeEmitter(*this, MCE)); 
  // Delete machine code for this function after emitting it
  PM.add(createMachineCodeDeleter());
  return false;
}

bool PPC32CodeEmitter::runOnMachineFunction(MachineFunction &MF) {
  MCE.startFunction(MF);
  MCE.emitConstantPool(MF.getConstantPool());
  for (MachineFunction::iterator BB = MF.begin(), E = MF.end(); BB != E; ++BB)
    emitBasicBlock(*BB);
  MCE.finishFunction(MF);

  // Resolve branches to BasicBlocks for the entire function
  for (unsigned i = 0, e = BBRefs.size(); i != e; ++i) {
    long Location = BBLocations[BBRefs[i].first];
    unsigned *Ref = BBRefs[i].second.first;
    MachineInstr *MI = BBRefs[i].second.second;
    DEBUG(std::cerr << "Fixup @ " << std::hex << Ref << " to 0x" << Location
                    << " in instr: " << std::dec << *MI);
    for (unsigned ii = 0, ee = MI->getNumOperands(); ii != ee; ++ii) {
      MachineOperand &op = MI->getOperand(ii);
      if (op.isPCRelativeDisp()) {
        // the instruction's branch target is made such that it branches to
        // PC + (branchTarget * 4), so undo that arithmetic here:
        // Location is the target of the branch
        // Ref is the location of the instruction, and hence the PC
        int64_t branchTarget = (Location - (long)Ref) >> 2;
        MI->SetMachineOperandConst(ii, MachineOperand::MO_SignExtendedImmed,
                                   branchTarget);
        unsigned fixedInstr = PPC32CodeEmitter::getBinaryCodeForInstr(*MI);
        MCE.emitWordAt(fixedInstr, Ref);
        break;
      }
    }
  }
  BBRefs.clear();
  BBLocations.clear();

  return false;
}

void PPC32CodeEmitter::emitBasicBlock(MachineBasicBlock &MBB) {
  BBLocations[MBB.getBasicBlock()] = MCE.getCurrentPCValue();
  for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E; ++I){
    MachineInstr &MI = *I;
    unsigned Opcode = MI.getOpcode();
    if (Opcode == PPC::IMPLICIT_DEF) 
      continue; // pseudo opcode, no side effects
    else if (Opcode == PPC::MovePCtoLR) {
      // This can be simplified: the resulting 32-bit code is 0x48000005
      MachineInstr *MI = BuildMI(PPC::BL, 1).addImm(1);
      emitWord(getBinaryCodeForInstr(*MI));
      delete MI;
    } else
      emitWord(getBinaryCodeForInstr(*I));
  }
}

unsigned PPC32CodeEmitter::getAddressOfExternalFunction(Function *F) {
  static std::map<Function*, unsigned> ExternalFn2Addr;
  std::map<Function*, unsigned>::iterator Addr = ExternalFn2Addr.find(F);

  if (Addr == ExternalFn2Addr.end())
    ExternalFn2Addr[F] = MCE.forceCompilationOf(F);
  return ExternalFn2Addr[F];
}

static unsigned enumRegToMachineReg(unsigned enumReg) {
  switch (enumReg) {
  case PPC::R0 :  case PPC::F0 :  return  0;  
  case PPC::R1 :  case PPC::F1 :  return  1; 
  case PPC::R2 :  case PPC::F2 :  return  2;
  case PPC::R3 :  case PPC::F3 :  return  3; 
  case PPC::R4 :  case PPC::F4 :  return  4; 
  case PPC::R5 :  case PPC::F5 :  return  5;
  case PPC::R6 :  case PPC::F6 :  return  6; 
  case PPC::R7 :  case PPC::F7 :  return  7; 
  case PPC::R8 :  case PPC::F8 :  return  8;
  case PPC::R9 :  case PPC::F9 :  return  9; 
  case PPC::R10:  case PPC::F10:  return 10; 
  case PPC::R11:  case PPC::F11:  return 11;
  case PPC::R12:  case PPC::F12:  return 12; 
  case PPC::R13:  case PPC::F13:  return 13; 
  case PPC::R14:  case PPC::F14:  return 14;
  case PPC::R15:  case PPC::F15:  return 15; 
  case PPC::R16:  case PPC::F16:  return 16; 
  case PPC::R17:  case PPC::F17:  return 17;
  case PPC::R18:  case PPC::F18:  return 18; 
  case PPC::R19:  case PPC::F19:  return 19; 
  case PPC::R20:  case PPC::F20:  return 20;
  case PPC::R21:  case PPC::F21:  return 21;
  case PPC::R22:  case PPC::F22:  return 22; 
  case PPC::R23:  case PPC::F23:  return 23; 
  case PPC::R24:  case PPC::F24:  return 24;
  case PPC::R25:  case PPC::F25:  return 25; 
  case PPC::R26:  case PPC::F26:  return 26; 
  case PPC::R27:  case PPC::F27:  return 27;
  case PPC::R28:  case PPC::F28:  return 28; 
  case PPC::R29:  case PPC::F29:  return 29; 
  case PPC::R30:  case PPC::F30:  return 30;
  case PPC::R31:  case PPC::F31:  return 31;
  default:
    std::cerr << "Unhandled reg in enumRegToRealReg!\n";
    abort();
  }
}

int64_t PPC32CodeEmitter::getMachineOpValue(MachineInstr &MI, 
                                            MachineOperand &MO) {
  int64_t rv = 0; // Return value; defaults to 0 for unhandled cases
                  // or things that get fixed up later by the JIT.
  if (MO.isRegister()) {
    rv = enumRegToMachineReg(MO.getReg());
  } else if (MO.isImmediate()) {
    rv = MO.getImmedValue();
  } else if (MO.isGlobalAddress()) {
    GlobalValue *GV = MO.getGlobal();
    rv = MCE.getGlobalValueAddress(GV);
    if (rv == 0) {
      if (Function *F = dyn_cast<Function>(GV)) {
        if (F->isExternal())
          rv = getAddressOfExternalFunction(F);
        else {
          // Function has not yet been code generated!  Use lazy resolution.
          getResolver(MCE).addFunctionReference(MCE.getCurrentPCValue(), F);
          rv = getResolver(MCE).getLazyResolver(F);
        }
      } else if (GlobalVariable *GVar = dyn_cast<GlobalVariable>(GV)) {
        if (GVar->isExternal()) {
          rv = MCE.getGlobalValueAddress(MO.getSymbolName());
          if (!rv) {
            std::cerr << "PPC32CodeEmitter: External global addr not found: " 
                      << *GVar;
            abort();
          }
        } else {
          std::cerr << "PPC32CodeEmitter: global addr not found: " << *GVar;
          abort();
        }
      }
    }
    if (MO.isPCRelative()) { // Global variable reference
      rv = (rv - MCE.getCurrentPCValue()) >> 2;
    }
  } else if (MO.isMachineBasicBlock()) {
    const BasicBlock *BB = MO.getMachineBasicBlock()->getBasicBlock();
    unsigned* CurrPC = (unsigned*)(intptr_t)MCE.getCurrentPCValue();
    BBRefs.push_back(std::make_pair(BB, std::make_pair(CurrPC, &MI)));
  } else if (MO.isConstantPoolIndex()) {
    unsigned index = MO.getConstantPoolIndex();
    rv = MCE.getConstantPoolEntryAddress(index);
  } else if (MO.isFrameIndex()) {
    std::cerr << "PPC32CodeEmitter: error: Frame index unhandled!\n";
    abort();
  } else {
    std::cerr << "ERROR: Unknown type of MachineOperand: " << MO << "\n";
    abort();
  }

  // Special treatment for global symbols: constants and vars
  if (MO.isConstantPoolIndex() || MO.isGlobalAddress()) {
    unsigned Opcode = MI.getOpcode();
    int64_t MBBLoc = BBLocations[MI.getParent()->getBasicBlock()];
    if (Opcode == PPC::LOADHiAddr) {
      // LoadHiAddr wants hi16(addr - mbb)
      rv = (rv - MBBLoc) >> 16;
    } else if (Opcode == PPC::LWZ || Opcode == PPC::LA ||
               Opcode == PPC::LFS || Opcode == PPC::LFD) {
      // These load opcodes want lo16(addr - mbb)
      rv = (rv - MBBLoc) & 0xffff;
    }
  }

  return rv;
}


void *PPC32JITInfo::getJITStubForFunction(Function *F, MachineCodeEmitter &MCE){
  return (void*)((unsigned long)getResolver(MCE).getLazyResolver(F));
}

void PPC32JITInfo::replaceMachineCodeForFunction (void *Old, void *New) {
  std::cerr << "PPC32JITInfo::replaceMachineCodeForFunction not implemented\n";
  abort();
}

#include "PPC32GenCodeEmitter.inc"

