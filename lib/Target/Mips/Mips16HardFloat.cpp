//===---- Mips16HardFloat.cpp for Mips16 Hard Float               --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass needed for Mips16 Hard Float
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "mips16-hard-float"
#include "Mips16HardFloat.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

static void inlineAsmOut
  (LLVMContext &C, StringRef AsmString, BasicBlock *BB ) {
  std::vector<llvm::Type *> AsmArgTypes;
  std::vector<llvm::Value*> AsmArgs;
  llvm::FunctionType *AsmFTy =
    llvm::FunctionType::get(Type::getVoidTy(C),
                            AsmArgTypes, false);
  llvm::InlineAsm *IA =
    llvm::InlineAsm::get(AsmFTy, AsmString, "", true,
                         /* IsAlignStack */ false,
                         llvm::InlineAsm::AD_ATT);
  CallInst::Create(IA, AsmArgs, "", BB);
}

namespace {

class InlineAsmHelper {
  LLVMContext &C;
  BasicBlock *BB;
public:
  InlineAsmHelper(LLVMContext &C_, BasicBlock *BB_) :
    C(C_), BB(BB_) {
  }

  void Out(StringRef AsmString) {
    inlineAsmOut(C, AsmString, BB);
  }

};
}
//
// Return types that matter for hard float are:
// float, double, complex float, and complex double
//
enum FPReturnVariant {
  FRet, DRet, CFRet, CDRet, NoFPRet
};

//
// Determine which FP return type this function has
//
static FPReturnVariant whichFPReturnVariant(Type *T) {
  switch (T->getTypeID()) {
  case Type::FloatTyID:
    return FRet;
  case Type::DoubleTyID:
    return DRet;
  case Type::StructTyID:
    if (T->getStructNumElements() != 2)
      break;
    if ((T->getContainedType(0)->isFloatTy()) &&
        (T->getContainedType(1)->isFloatTy()))
      return CFRet;
    if ((T->getContainedType(0)->isDoubleTy()) &&
        (T->getContainedType(1)->isDoubleTy()))
      return CDRet;
    break;
  default:
    break;
  }
  return NoFPRet;
}

//
// Parameter type that matter are float, (float, float), (float, double),
// double, (double, double), (double, float)
//
enum FPParamVariant {
  FSig, FFSig, FDSig,
  DSig, DDSig, DFSig, NoSig
};

// which floating point parameter signature variant we are dealing with
//
typedef Type::TypeID TypeID;
const Type::TypeID FloatTyID = Type::FloatTyID;
const Type::TypeID DoubleTyID = Type::DoubleTyID;

static FPParamVariant whichFPParamVariantNeeded(Function &F) {
  switch (F.arg_size()) {
  case 0:
    return NoSig;
  case 1:{
    TypeID ArgTypeID = F.getFunctionType()->getParamType(0)->getTypeID();
    switch (ArgTypeID) {
    case FloatTyID:
      return FSig;
    case DoubleTyID:
      return DSig;
    default:
      return NoSig;
    }
  }
  default: {
    TypeID ArgTypeID0 = F.getFunctionType()->getParamType(0)->getTypeID();
    TypeID ArgTypeID1 = F.getFunctionType()->getParamType(1)->getTypeID();
    switch(ArgTypeID0) {
    case FloatTyID: {
      switch (ArgTypeID1) {
      case FloatTyID:
        return FFSig;
      case DoubleTyID:
        return FDSig;
      default:
        return FSig;
      }
    }
    case DoubleTyID: {
      switch (ArgTypeID1) {
      case FloatTyID:
        return DFSig;
      case DoubleTyID:
        return DDSig;
      default:
        return DSig;
      }
    }
    default:
      return NoSig;
    }
  }
  }
  llvm_unreachable("can't get here");
}

// Figure out if we need float point based on the function parameters.
// We need to move variables in and/or out of floating point
// registers because of the ABI
//
static bool needsFPStubFromParams(Function &F) {
  if (F.arg_size() >=1) {
    Type *ArgType = F.getFunctionType()->getParamType(0);
    switch (ArgType->getTypeID()) {
      case Type::FloatTyID:
      case Type::DoubleTyID:
        return true;
      default:
        break;
    }
  }
  return false;
}

static bool needsFPReturnHelper(Function &F) {
  Type* RetType = F.getReturnType();
  return whichFPReturnVariant(RetType) != NoFPRet;
}

static bool needsFPHelperFromSig(Function &F) {
  return needsFPStubFromParams(F) || needsFPReturnHelper(F);
}

//
// We swap between FP and Integer registers to allow Mips16 and Mips32 to
// interoperate
//

static void swapFPIntParams
  (FPParamVariant PV, Module *M, InlineAsmHelper &IAH,
   bool LE, bool ToFP) {
  //LLVMContext &Context = M->getContext();
  std::string MI = ToFP? "mtc1 ": "mfc1 ";
  switch (PV) {
  case FSig:
    IAH.Out(MI + "$$4,$$f12");
    break;
  case FFSig:
    IAH.Out(MI +"$$4,$$f12");
    IAH.Out(MI + "$$5,$$f14");
    break;
  case FDSig:
    IAH.Out(MI + "$$4,$$f12");
    if (LE) {
      IAH.Out(MI + "$$6,$$f14");
      IAH.Out(MI + "$$7,$$f15");
    } else {
      IAH.Out(MI + "$$7,$$f14");
      IAH.Out(MI + "$$6,$$f15");
    }
    break;
  case DSig:
    if (LE) {
      IAH.Out(MI + "$$4,$$f12");
      IAH.Out(MI + "$$5,$$f13");
    } else {
      IAH.Out(MI + "$$5,$$f12");
      IAH.Out(MI + "$$4,$$f13");
    }
    break;
  case DDSig:
    if (LE) {
      IAH.Out(MI + "$$4,$$f12");
      IAH.Out(MI + "$$5,$$f13");
      IAH.Out(MI + "$$6,$$f14");
      IAH.Out(MI + "$$7,$$f15");
    } else {
      IAH.Out(MI + "$$5,$$f12");
      IAH.Out(MI + "$$4,$$f13");
      IAH.Out(MI + "$$7,$$f14");
      IAH.Out(MI + "$$6,$$f15");
    }
    break;
  case DFSig:
    if (LE) {
      IAH.Out(MI + "$$4,$$f12");
      IAH.Out(MI + "$$5,$$f13");
    } else {
      IAH.Out(MI + "$$5,$$f12");
      IAH.Out(MI + "$$4,$$f13");
    }
    IAH.Out(MI + "$$6,$$f14");
    break;
  case NoSig:
    return;
  }
}
//
// Make sure that we know we already need a stub for this function.
// Having called needsFPHelperFromSig
//
static void assureFPCallStub(Function &F, Module *M,  
                             const MipsSubtarget &Subtarget){
  // for now we only need them for static relocation
  if (Subtarget.getRelocationModel() == Reloc::PIC_)
    return;
  LLVMContext &Context = M->getContext();
  bool LE = Subtarget.isLittle();
  std::string Name = F.getName();
  std::string SectionName = ".mips16.call.fp." + Name;
  std::string StubName = "__call_stub_" + Name;
  //
  // see if we already have the stub
  //
  Function *FStub = M->getFunction(StubName);
  if (FStub && !FStub->isDeclaration()) return;
  FStub = Function::Create(F.getFunctionType(),
                           Function::InternalLinkage, StubName, M);
  FStub->addFnAttr("mips16_fp_stub");
  FStub->addFnAttr(llvm::Attribute::Naked);
  FStub->addFnAttr(llvm::Attribute::NoUnwind);
  FStub->addFnAttr("nomips16");
  FStub->setSection(SectionName);
  BasicBlock *BB = BasicBlock::Create(Context, "entry", FStub);
  InlineAsmHelper IAH(Context, BB);
  FPReturnVariant RV = whichFPReturnVariant(FStub->getReturnType());
  FPParamVariant PV = whichFPParamVariantNeeded(F);
  swapFPIntParams(PV, M, IAH, LE, true);
  if (RV != NoFPRet) {
    IAH.Out("move $$18, $$31");
    IAH.Out("jal " + Name);
  } else {
    IAH.Out("lui  $$25,%hi(" + Name + ")");
    IAH.Out("addiu  $$25,$$25,%lo(" + Name + ")" );
  }
  switch (RV) {
  case FRet:
    IAH.Out("mfc1 $$2,$$f0");
    break;
  case DRet:
    if (LE) {
      IAH.Out("mfc1 $$2,$$f0");
      IAH.Out("mfc1 $$3,$$f1");
    } else {
      IAH.Out("mfc1 $$3,$$f0");
      IAH.Out("mfc1 $$2,$$f1");
    }
    break;
  case CFRet:
    if (LE) {
    IAH.Out("mfc1 $$2,$$f0");
    IAH.Out("mfc1 $$3,$$f2");
    } else {
      IAH.Out("mfc1 $$3,$$f0");
      IAH.Out("mfc1 $$3,$$f2");
    }
    break;
  case CDRet:
    if (LE) {
      IAH.Out("mfc1 $$4,$$f2");
      IAH.Out("mfc1 $$5,$$f3");
      IAH.Out("mfc1 $$2,$$f0");
      IAH.Out("mfc1 $$3,$$f1");

    } else {
      IAH.Out("mfc1 $$5,$$f2");
      IAH.Out("mfc1 $$4,$$f3");
      IAH.Out("mfc1 $$3,$$f0");
      IAH.Out("mfc1 $$2,$$f1");
    }
    break;
  case NoFPRet:
    break;
  }
  if (RV != NoFPRet)
    IAH.Out("jr $$18");
  else
    IAH.Out("jr $$25");
  new UnreachableInst(Context, BB);
}

//
// Returns of float, double and complex need to be handled with a helper
// function.
//
static bool fixupFPReturnAndCall
  (Function &F, Module *M,  const MipsSubtarget &Subtarget) {
  bool Modified = false;
  LLVMContext &C = M->getContext();
  Type *MyVoid = Type::getVoidTy(C);
  for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB)
    for (BasicBlock::iterator I = BB->begin(), E = BB->end();
         I != E; ++I) {
      Instruction &Inst = *I;
      if (const ReturnInst *RI = dyn_cast<ReturnInst>(I)) {
        Value *RVal = RI->getReturnValue();
        if (!RVal) continue;
        //
        // If there is a return value and it needs a helper function,
        // figure out which one and add a call before the actual
        // return to this helper. The purpose of the helper is to move
        // floating point values from their soft float return mapping to
        // where they would have been mapped to in floating point registers.
        //
        Type *T = RVal->getType();
        FPReturnVariant RV = whichFPReturnVariant(T);
        if (RV == NoFPRet) continue;
        static const char* Helper[NoFPRet] =
          {"__mips16_ret_sf", "__mips16_ret_df", "__mips16_ret_sc",
           "__mips16_ret_dc"};
        const char *Name = Helper[RV];
        AttributeSet A;
        Value *Params[] = {RVal};
        Modified = true;
        //
        // These helper functions have a different calling ABI so
        // this __Mips16RetHelper indicates that so that later
        // during call setup, the proper call lowering to the helper
        // functions will take place.
        //
        A = A.addAttribute(C, AttributeSet::FunctionIndex,
                           "__Mips16RetHelper");
        A = A.addAttribute(C, AttributeSet::FunctionIndex,
                           Attribute::ReadNone);
        Value *F = (M->getOrInsertFunction(Name, A, MyVoid, T, NULL));
        CallInst::Create(F, Params, "", &Inst );
      } else if (const CallInst *CI = dyn_cast<CallInst>(I)) {
          // pic mode calls are handled by already defined
          // helper functions
          if (Subtarget.getRelocationModel() != Reloc::PIC_ ) {
            Function *F_ =  CI->getCalledFunction();
            if (F_ && needsFPHelperFromSig(*F_)) {
              assureFPCallStub(*F_, M, Subtarget);
              Modified=true;
            }
          }
      }
    }
  return Modified;
}

static void createFPFnStub(Function *F, Module *M, FPParamVariant PV,
                  const MipsSubtarget &Subtarget ) {
  bool PicMode = Subtarget.getRelocationModel() == Reloc::PIC_;
  bool LE = Subtarget.isLittle();
  LLVMContext &Context = M->getContext();
  std::string Name = F->getName();
  std::string SectionName = ".mips16.fn." + Name;
  std::string StubName = "__fn_stub_" + Name;
  std::string LocalName = "__fn_local_" + Name;
  Function *FStub = Function::Create
    (F->getFunctionType(),
     Function::ExternalLinkage, StubName, M);
  FStub->addFnAttr("mips16_fp_stub");
  FStub->addFnAttr(llvm::Attribute::Naked);
  FStub->addFnAttr(llvm::Attribute::NoUnwind);
  FStub->addFnAttr("nomips16");
  FStub->setSection(SectionName);
  BasicBlock *BB = BasicBlock::Create(Context, "entry", FStub);
  InlineAsmHelper IAH(Context, BB);
  IAH.Out(" .set  macro");
  if (PicMode) {
    IAH.Out(".set noreorder");
    IAH.Out(".cpload  $$2");
    IAH.Out(".set reorder");
    IAH.Out(".reloc 0,R_MIPS_NONE," + Name);
    IAH.Out("la $$25," + LocalName);
  }
  else
    IAH.Out("la $$25, " + Name);
  swapFPIntParams(PV, M, IAH, LE, false);
  IAH.Out("jr $$25");
  IAH.Out(LocalName + " = " + Name);
  new UnreachableInst(FStub->getContext(), BB);
}

namespace llvm {

//
// This pass only makes sense when the underlying chip has floating point but
// we are compiling as mips16.
// For all mips16 functions (that are not stubs we have already generated), or
// declared via attributes as nomips16, we must:
//    1) fixup all returns of float, double, single and double complex
//       by calling a helper function before the actual return.
//    2) generate helper functions (stubs) that can be called by mips32 functions
//       that will move parameters passed normally passed in floating point
//       registers the soft float equivalents.
//    3) in the case of static relocation, generate helper functions so that
//       mips16 functions can call extern functions of unknown type (mips16 or
//       mips32).
//    4) TBD. For pic, calls to extern functions of unknown type are handled by
//       predefined helper functions in libc but this work is currently done
//       during call lowering but it should be moved here in the future.
//
bool Mips16HardFloat::runOnModule(Module &M) {
  DEBUG(errs() << "Run on Module Mips16HardFloat\n");
  bool Modified = false;
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (F->isDeclaration() || F->hasFnAttribute("mips16_fp_stub") ||
        F->hasFnAttribute("nomips16")) continue;
    Modified |= fixupFPReturnAndCall(*F, &M, Subtarget);
    FPParamVariant V = whichFPParamVariantNeeded(*F);
    if (V != NoSig) {
      Modified = true;
      createFPFnStub(F, &M, V, Subtarget);
    }
  }
  return Modified;
}

char Mips16HardFloat::ID = 0;

}

ModulePass *llvm::createMips16HardFloat(MipsTargetMachine &TM) {
  return new Mips16HardFloat(TM);
}

