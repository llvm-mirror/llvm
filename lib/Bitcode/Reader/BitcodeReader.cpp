//===- BitcodeReader.cpp - Internal BitcodeReader implementation ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/ReaderWriter.h"
#include "BitcodeReader.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/AutoUpgrade.h"
#include "llvm/Bitcode/LLVMBitCodes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/DataStream.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

enum {
  SWITCH_INST_MAGIC = 0x4B5 // May 2012 => 1205 => Hex
};

void BitcodeReader::materializeForwardReferencedFunctions() {
  while (!BlockAddrFwdRefs.empty()) {
    Function *F = BlockAddrFwdRefs.begin()->first;
    F->Materialize();
  }
}

void BitcodeReader::FreeState() {
  if (BufferOwned)
    delete Buffer;
  Buffer = 0;
  std::vector<Type*>().swap(TypeList);
  ValueList.clear();
  MDValueList.clear();

  std::vector<AttributeSet>().swap(MAttributes);
  std::vector<BasicBlock*>().swap(FunctionBBs);
  std::vector<Function*>().swap(FunctionsWithBodies);
  DeferredFunctionInfo.clear();
  MDKindMap.clear();

  assert(BlockAddrFwdRefs.empty() && "Unresolved blockaddress fwd references");
}

//===----------------------------------------------------------------------===//
//  Helper functions to implement forward reference resolution, etc.
//===----------------------------------------------------------------------===//

/// ConvertToString - Convert a string from a record into an std::string, return
/// true on failure.
template<typename StrTy>
static bool ConvertToString(ArrayRef<uint64_t> Record, unsigned Idx,
                            StrTy &Result) {
  if (Idx > Record.size())
    return true;

  for (unsigned i = Idx, e = Record.size(); i != e; ++i)
    Result += (char)Record[i];
  return false;
}

static GlobalValue::LinkageTypes GetDecodedLinkage(unsigned Val) {
  switch (Val) {
  default: // Map unknown/new linkages to external
  case 0:  return GlobalValue::ExternalLinkage;
  case 1:  return GlobalValue::WeakAnyLinkage;
  case 2:  return GlobalValue::AppendingLinkage;
  case 3:  return GlobalValue::InternalLinkage;
  case 4:  return GlobalValue::LinkOnceAnyLinkage;
  case 5:  return GlobalValue::DLLImportLinkage;
  case 6:  return GlobalValue::DLLExportLinkage;
  case 7:  return GlobalValue::ExternalWeakLinkage;
  case 8:  return GlobalValue::CommonLinkage;
  case 9:  return GlobalValue::PrivateLinkage;
  case 10: return GlobalValue::WeakODRLinkage;
  case 11: return GlobalValue::LinkOnceODRLinkage;
  case 12: return GlobalValue::AvailableExternallyLinkage;
  case 13: return GlobalValue::LinkerPrivateLinkage;
  case 14: return GlobalValue::LinkerPrivateWeakLinkage;
  case 15: return GlobalValue::LinkOnceODRAutoHideLinkage;
  }
}

static GlobalValue::VisibilityTypes GetDecodedVisibility(unsigned Val) {
  switch (Val) {
  default: // Map unknown visibilities to default.
  case 0: return GlobalValue::DefaultVisibility;
  case 1: return GlobalValue::HiddenVisibility;
  case 2: return GlobalValue::ProtectedVisibility;
  }
}

static GlobalVariable::ThreadLocalMode GetDecodedThreadLocalMode(unsigned Val) {
  switch (Val) {
    case 0: return GlobalVariable::NotThreadLocal;
    default: // Map unknown non-zero value to general dynamic.
    case 1: return GlobalVariable::GeneralDynamicTLSModel;
    case 2: return GlobalVariable::LocalDynamicTLSModel;
    case 3: return GlobalVariable::InitialExecTLSModel;
    case 4: return GlobalVariable::LocalExecTLSModel;
  }
}

static int GetDecodedCastOpcode(unsigned Val) {
  switch (Val) {
  default: return -1;
  case bitc::CAST_TRUNC   : return Instruction::Trunc;
  case bitc::CAST_ZEXT    : return Instruction::ZExt;
  case bitc::CAST_SEXT    : return Instruction::SExt;
  case bitc::CAST_FPTOUI  : return Instruction::FPToUI;
  case bitc::CAST_FPTOSI  : return Instruction::FPToSI;
  case bitc::CAST_UITOFP  : return Instruction::UIToFP;
  case bitc::CAST_SITOFP  : return Instruction::SIToFP;
  case bitc::CAST_FPTRUNC : return Instruction::FPTrunc;
  case bitc::CAST_FPEXT   : return Instruction::FPExt;
  case bitc::CAST_PTRTOINT: return Instruction::PtrToInt;
  case bitc::CAST_INTTOPTR: return Instruction::IntToPtr;
  case bitc::CAST_BITCAST : return Instruction::BitCast;
  }
}
static int GetDecodedBinaryOpcode(unsigned Val, Type *Ty) {
  switch (Val) {
  default: return -1;
  case bitc::BINOP_ADD:
    return Ty->isFPOrFPVectorTy() ? Instruction::FAdd : Instruction::Add;
  case bitc::BINOP_SUB:
    return Ty->isFPOrFPVectorTy() ? Instruction::FSub : Instruction::Sub;
  case bitc::BINOP_MUL:
    return Ty->isFPOrFPVectorTy() ? Instruction::FMul : Instruction::Mul;
  case bitc::BINOP_UDIV: return Instruction::UDiv;
  case bitc::BINOP_SDIV:
    return Ty->isFPOrFPVectorTy() ? Instruction::FDiv : Instruction::SDiv;
  case bitc::BINOP_UREM: return Instruction::URem;
  case bitc::BINOP_SREM:
    return Ty->isFPOrFPVectorTy() ? Instruction::FRem : Instruction::SRem;
  case bitc::BINOP_SHL:  return Instruction::Shl;
  case bitc::BINOP_LSHR: return Instruction::LShr;
  case bitc::BINOP_ASHR: return Instruction::AShr;
  case bitc::BINOP_AND:  return Instruction::And;
  case bitc::BINOP_OR:   return Instruction::Or;
  case bitc::BINOP_XOR:  return Instruction::Xor;
  }
}

static AtomicRMWInst::BinOp GetDecodedRMWOperation(unsigned Val) {
  switch (Val) {
  default: return AtomicRMWInst::BAD_BINOP;
  case bitc::RMW_XCHG: return AtomicRMWInst::Xchg;
  case bitc::RMW_ADD: return AtomicRMWInst::Add;
  case bitc::RMW_SUB: return AtomicRMWInst::Sub;
  case bitc::RMW_AND: return AtomicRMWInst::And;
  case bitc::RMW_NAND: return AtomicRMWInst::Nand;
  case bitc::RMW_OR: return AtomicRMWInst::Or;
  case bitc::RMW_XOR: return AtomicRMWInst::Xor;
  case bitc::RMW_MAX: return AtomicRMWInst::Max;
  case bitc::RMW_MIN: return AtomicRMWInst::Min;
  case bitc::RMW_UMAX: return AtomicRMWInst::UMax;
  case bitc::RMW_UMIN: return AtomicRMWInst::UMin;
  }
}

static AtomicOrdering GetDecodedOrdering(unsigned Val) {
  switch (Val) {
  case bitc::ORDERING_NOTATOMIC: return NotAtomic;
  case bitc::ORDERING_UNORDERED: return Unordered;
  case bitc::ORDERING_MONOTONIC: return Monotonic;
  case bitc::ORDERING_ACQUIRE: return Acquire;
  case bitc::ORDERING_RELEASE: return Release;
  case bitc::ORDERING_ACQREL: return AcquireRelease;
  default: // Map unknown orderings to sequentially-consistent.
  case bitc::ORDERING_SEQCST: return SequentiallyConsistent;
  }
}

static SynchronizationScope GetDecodedSynchScope(unsigned Val) {
  switch (Val) {
  case bitc::SYNCHSCOPE_SINGLETHREAD: return SingleThread;
  default: // Map unknown scopes to cross-thread.
  case bitc::SYNCHSCOPE_CROSSTHREAD: return CrossThread;
  }
}

namespace llvm {
namespace {
  /// @brief A class for maintaining the slot number definition
  /// as a placeholder for the actual definition for forward constants defs.
  class ConstantPlaceHolder : public ConstantExpr {
    void operator=(const ConstantPlaceHolder &) LLVM_DELETED_FUNCTION;
  public:
    // allocate space for exactly one operand
    void *operator new(size_t s) {
      return User::operator new(s, 1);
    }
    explicit ConstantPlaceHolder(Type *Ty, LLVMContext& Context)
      : ConstantExpr(Ty, Instruction::UserOp1, &Op<0>(), 1) {
      Op<0>() = UndefValue::get(Type::getInt32Ty(Context));
    }

    /// @brief Methods to support type inquiry through isa, cast, and dyn_cast.
    static bool classof(const Value *V) {
      return isa<ConstantExpr>(V) &&
             cast<ConstantExpr>(V)->getOpcode() == Instruction::UserOp1;
    }


    /// Provide fast operand accessors
    //DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);
  };
}

// FIXME: can we inherit this from ConstantExpr?
template <>
struct OperandTraits<ConstantPlaceHolder> :
  public FixedNumOperandTraits<ConstantPlaceHolder, 1> {
};
}


void BitcodeReaderValueList::AssignValue(Value *V, unsigned Idx) {
  if (Idx == size()) {
    push_back(V);
    return;
  }

  if (Idx >= size())
    resize(Idx+1);

  WeakVH &OldV = ValuePtrs[Idx];
  if (OldV == 0) {
    OldV = V;
    return;
  }

  // Handle constants and non-constants (e.g. instrs) differently for
  // efficiency.
  if (Constant *PHC = dyn_cast<Constant>(&*OldV)) {
    ResolveConstants.push_back(std::make_pair(PHC, Idx));
    OldV = V;
  } else {
    // If there was a forward reference to this value, replace it.
    Value *PrevVal = OldV;
    OldV->replaceAllUsesWith(V);
    delete PrevVal;
  }
}


Constant *BitcodeReaderValueList::getConstantFwdRef(unsigned Idx,
                                                    Type *Ty) {
  if (Idx >= size())
    resize(Idx + 1);

  if (Value *V = ValuePtrs[Idx]) {
    assert(Ty == V->getType() && "Type mismatch in constant table!");
    return cast<Constant>(V);
  }

  // Create and return a placeholder, which will later be RAUW'd.
  Constant *C = new ConstantPlaceHolder(Ty, Context);
  ValuePtrs[Idx] = C;
  return C;
}

Value *BitcodeReaderValueList::getValueFwdRef(unsigned Idx, Type *Ty) {
  if (Idx >= size())
    resize(Idx + 1);

  if (Value *V = ValuePtrs[Idx]) {
    assert((Ty == 0 || Ty == V->getType()) && "Type mismatch in value table!");
    return V;
  }

  // No type specified, must be invalid reference.
  if (Ty == 0) return 0;

  // Create and return a placeholder, which will later be RAUW'd.
  Value *V = new Argument(Ty);
  ValuePtrs[Idx] = V;
  return V;
}

/// ResolveConstantForwardRefs - Once all constants are read, this method bulk
/// resolves any forward references.  The idea behind this is that we sometimes
/// get constants (such as large arrays) which reference *many* forward ref
/// constants.  Replacing each of these causes a lot of thrashing when
/// building/reuniquing the constant.  Instead of doing this, we look at all the
/// uses and rewrite all the place holders at once for any constant that uses
/// a placeholder.
void BitcodeReaderValueList::ResolveConstantForwardRefs() {
  // Sort the values by-pointer so that they are efficient to look up with a
  // binary search.
  std::sort(ResolveConstants.begin(), ResolveConstants.end());

  SmallVector<Constant*, 64> NewOps;

  while (!ResolveConstants.empty()) {
    Value *RealVal = operator[](ResolveConstants.back().second);
    Constant *Placeholder = ResolveConstants.back().first;
    ResolveConstants.pop_back();

    // Loop over all users of the placeholder, updating them to reference the
    // new value.  If they reference more than one placeholder, update them all
    // at once.
    while (!Placeholder->use_empty()) {
      Value::use_iterator UI = Placeholder->use_begin();
      User *U = *UI;

      // If the using object isn't uniqued, just update the operands.  This
      // handles instructions and initializers for global variables.
      if (!isa<Constant>(U) || isa<GlobalValue>(U)) {
        UI.getUse().set(RealVal);
        continue;
      }

      // Otherwise, we have a constant that uses the placeholder.  Replace that
      // constant with a new constant that has *all* placeholder uses updated.
      Constant *UserC = cast<Constant>(U);
      for (User::op_iterator I = UserC->op_begin(), E = UserC->op_end();
           I != E; ++I) {
        Value *NewOp;
        if (!isa<ConstantPlaceHolder>(*I)) {
          // Not a placeholder reference.
          NewOp = *I;
        } else if (*I == Placeholder) {
          // Common case is that it just references this one placeholder.
          NewOp = RealVal;
        } else {
          // Otherwise, look up the placeholder in ResolveConstants.
          ResolveConstantsTy::iterator It =
            std::lower_bound(ResolveConstants.begin(), ResolveConstants.end(),
                             std::pair<Constant*, unsigned>(cast<Constant>(*I),
                                                            0));
          assert(It != ResolveConstants.end() && It->first == *I);
          NewOp = operator[](It->second);
        }

        NewOps.push_back(cast<Constant>(NewOp));
      }

      // Make the new constant.
      Constant *NewC;
      if (ConstantArray *UserCA = dyn_cast<ConstantArray>(UserC)) {
        NewC = ConstantArray::get(UserCA->getType(), NewOps);
      } else if (ConstantStruct *UserCS = dyn_cast<ConstantStruct>(UserC)) {
        NewC = ConstantStruct::get(UserCS->getType(), NewOps);
      } else if (isa<ConstantVector>(UserC)) {
        NewC = ConstantVector::get(NewOps);
      } else {
        assert(isa<ConstantExpr>(UserC) && "Must be a ConstantExpr.");
        NewC = cast<ConstantExpr>(UserC)->getWithOperands(NewOps);
      }

      UserC->replaceAllUsesWith(NewC);
      UserC->destroyConstant();
      NewOps.clear();
    }

    // Update all ValueHandles, they should be the only users at this point.
    Placeholder->replaceAllUsesWith(RealVal);
    delete Placeholder;
  }
}

void BitcodeReaderMDValueList::AssignValue(Value *V, unsigned Idx) {
  if (Idx == size()) {
    push_back(V);
    return;
  }

  if (Idx >= size())
    resize(Idx+1);

  WeakVH &OldV = MDValuePtrs[Idx];
  if (OldV == 0) {
    OldV = V;
    return;
  }

  // If there was a forward reference to this value, replace it.
  MDNode *PrevVal = cast<MDNode>(OldV);
  OldV->replaceAllUsesWith(V);
  MDNode::deleteTemporary(PrevVal);
  // Deleting PrevVal sets Idx value in MDValuePtrs to null. Set new
  // value for Idx.
  MDValuePtrs[Idx] = V;
}

Value *BitcodeReaderMDValueList::getValueFwdRef(unsigned Idx) {
  if (Idx >= size())
    resize(Idx + 1);

  if (Value *V = MDValuePtrs[Idx]) {
    assert(V->getType()->isMetadataTy() && "Type mismatch in value table!");
    return V;
  }

  // Create and return a placeholder, which will later be RAUW'd.
  Value *V = MDNode::getTemporary(Context, None);
  MDValuePtrs[Idx] = V;
  return V;
}

Type *BitcodeReader::getTypeByID(unsigned ID) {
  // The type table size is always specified correctly.
  if (ID >= TypeList.size())
    return 0;

  if (Type *Ty = TypeList[ID])
    return Ty;

  // If we have a forward reference, the only possible case is when it is to a
  // named struct.  Just create a placeholder for now.
  return TypeList[ID] = StructType::create(Context);
}


//===----------------------------------------------------------------------===//
//  Functions for parsing blocks from the bitcode file
//===----------------------------------------------------------------------===//


/// \brief This fills an AttrBuilder object with the LLVM attributes that have
/// been decoded from the given integer. This function must stay in sync with
/// 'encodeLLVMAttributesForBitcode'.
static void decodeLLVMAttributesForBitcode(AttrBuilder &B,
                                           uint64_t EncodedAttrs) {
  // FIXME: Remove in 4.0.

  // The alignment is stored as a 16-bit raw value from bits 31--16.  We shift
  // the bits above 31 down by 11 bits.
  unsigned Alignment = (EncodedAttrs & (0xffffULL << 16)) >> 16;
  assert((!Alignment || isPowerOf2_32(Alignment)) &&
         "Alignment must be a power of two.");

  if (Alignment)
    B.addAlignmentAttr(Alignment);
  B.addRawValue(((EncodedAttrs & (0xfffffULL << 32)) >> 11) |
                (EncodedAttrs & 0xffff));
}

bool BitcodeReader::ParseAttributeBlock() {
  if (Stream.EnterSubBlock(bitc::PARAMATTR_BLOCK_ID))
    return Error("Malformed block record");

  if (!MAttributes.empty())
    return Error("Multiple PARAMATTR blocks found!");

  SmallVector<uint64_t, 64> Record;

  SmallVector<AttributeSet, 8> Attrs;

  // Read all the records.
  while (1) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return Error("Error at end of PARAMATTR block");
    case BitstreamEntry::EndBlock:
      return false;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: ignore.
      break;
    case bitc::PARAMATTR_CODE_ENTRY_OLD: { // ENTRY: [paramidx0, attr0, ...]
      // FIXME: Remove in 4.0.
      if (Record.size() & 1)
        return Error("Invalid ENTRY record");

      for (unsigned i = 0, e = Record.size(); i != e; i += 2) {
        AttrBuilder B;
        decodeLLVMAttributesForBitcode(B, Record[i+1]);
        Attrs.push_back(AttributeSet::get(Context, Record[i], B));
      }

      MAttributes.push_back(AttributeSet::get(Context, Attrs));
      Attrs.clear();
      break;
    }
    case bitc::PARAMATTR_CODE_ENTRY: { // ENTRY: [attrgrp0, attrgrp1, ...]
      for (unsigned i = 0, e = Record.size(); i != e; ++i)
        Attrs.push_back(MAttributeGroups[Record[i]]);

      MAttributes.push_back(AttributeSet::get(Context, Attrs));
      Attrs.clear();
      break;
    }
    }
  }
}

bool BitcodeReader::ParseAttrKind(uint64_t Code, Attribute::AttrKind *Kind) {
  switch (Code) {
  case bitc::ATTR_KIND_ALIGNMENT:
    *Kind = Attribute::Alignment;
    return false;
  case bitc::ATTR_KIND_ALWAYS_INLINE:
    *Kind = Attribute::AlwaysInline;
    return false;
  case bitc::ATTR_KIND_BUILTIN:
    *Kind = Attribute::Builtin;
    return false;
  case bitc::ATTR_KIND_BY_VAL:
    *Kind = Attribute::ByVal;
    return false;
  case bitc::ATTR_KIND_COLD:
    *Kind = Attribute::Cold;
    return false;
  case bitc::ATTR_KIND_INLINE_HINT:
    *Kind = Attribute::InlineHint;
    return false;
  case bitc::ATTR_KIND_IN_REG:
    *Kind = Attribute::InReg;
    return false;
  case bitc::ATTR_KIND_MIN_SIZE:
    *Kind = Attribute::MinSize;
    return false;
  case bitc::ATTR_KIND_NAKED:
    *Kind = Attribute::Naked;
    return false;
  case bitc::ATTR_KIND_NEST:
    *Kind = Attribute::Nest;
    return false;
  case bitc::ATTR_KIND_NO_ALIAS:
    *Kind = Attribute::NoAlias;
    return false;
  case bitc::ATTR_KIND_NO_BUILTIN:
    *Kind = Attribute::NoBuiltin;
    return false;
  case bitc::ATTR_KIND_NO_CAPTURE:
    *Kind = Attribute::NoCapture;
    return false;
  case bitc::ATTR_KIND_NO_DUPLICATE:
    *Kind = Attribute::NoDuplicate;
    return false;
  case bitc::ATTR_KIND_NO_IMPLICIT_FLOAT:
    *Kind = Attribute::NoImplicitFloat;
    return false;
  case bitc::ATTR_KIND_NO_INLINE:
    *Kind = Attribute::NoInline;
    return false;
  case bitc::ATTR_KIND_NON_LAZY_BIND:
    *Kind = Attribute::NonLazyBind;
    return false;
  case bitc::ATTR_KIND_NO_RED_ZONE:
    *Kind = Attribute::NoRedZone;
    return false;
  case bitc::ATTR_KIND_NO_RETURN:
    *Kind = Attribute::NoReturn;
    return false;
  case bitc::ATTR_KIND_NO_UNWIND:
    *Kind = Attribute::NoUnwind;
    return false;
  case bitc::ATTR_KIND_OPTIMIZE_FOR_SIZE:
    *Kind = Attribute::OptimizeForSize;
    return false;
  case bitc::ATTR_KIND_OPTIMIZE_NONE:
    *Kind = Attribute::OptimizeNone;
    return false;
  case bitc::ATTR_KIND_READ_NONE:
    *Kind = Attribute::ReadNone;
    return false;
  case bitc::ATTR_KIND_READ_ONLY:
    *Kind = Attribute::ReadOnly;
    return false;
  case bitc::ATTR_KIND_RETURNED:
    *Kind = Attribute::Returned;
    return false;
  case bitc::ATTR_KIND_RETURNS_TWICE:
    *Kind = Attribute::ReturnsTwice;
    return false;
  case bitc::ATTR_KIND_S_EXT:
    *Kind = Attribute::SExt;
    return false;
  case bitc::ATTR_KIND_STACK_ALIGNMENT:
    *Kind = Attribute::StackAlignment;
    return false;
  case bitc::ATTR_KIND_STACK_PROTECT:
    *Kind = Attribute::StackProtect;
    return false;
  case bitc::ATTR_KIND_STACK_PROTECT_REQ:
    *Kind = Attribute::StackProtectReq;
    return false;
  case bitc::ATTR_KIND_STACK_PROTECT_STRONG:
    *Kind = Attribute::StackProtectStrong;
    return false;
  case bitc::ATTR_KIND_STRUCT_RET:
    *Kind = Attribute::StructRet;
    return false;
  case bitc::ATTR_KIND_SANITIZE_ADDRESS:
    *Kind = Attribute::SanitizeAddress;
    return false;
  case bitc::ATTR_KIND_SANITIZE_THREAD:
    *Kind = Attribute::SanitizeThread;
    return false;
  case bitc::ATTR_KIND_SANITIZE_MEMORY:
    *Kind = Attribute::SanitizeMemory;
    return false;
  case bitc::ATTR_KIND_UW_TABLE:
    *Kind = Attribute::UWTable;
    return false;
  case bitc::ATTR_KIND_Z_EXT:
    *Kind = Attribute::ZExt;
    return false;
  default:
    std::string Buf;
    raw_string_ostream fmt(Buf);
    fmt << "Unknown attribute kind (" << Code << ")";
    fmt.flush();
    return Error(Buf.c_str());
  }
}

bool BitcodeReader::ParseAttributeGroupBlock() {
  if (Stream.EnterSubBlock(bitc::PARAMATTR_GROUP_BLOCK_ID))
    return Error("Malformed block record");

  if (!MAttributeGroups.empty())
    return Error("Multiple PARAMATTR_GROUP blocks found!");

  SmallVector<uint64_t, 64> Record;

  // Read all the records.
  while (1) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return Error("Error at end of PARAMATTR_GROUP block");
    case BitstreamEntry::EndBlock:
      return false;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: ignore.
      break;
    case bitc::PARAMATTR_GRP_CODE_ENTRY: { // ENTRY: [grpid, idx, a0, a1, ...]
      if (Record.size() < 3)
        return Error("Invalid ENTRY record");

      uint64_t GrpID = Record[0];
      uint64_t Idx = Record[1]; // Index of the object this attribute refers to.

      AttrBuilder B;
      for (unsigned i = 2, e = Record.size(); i != e; ++i) {
        if (Record[i] == 0) {        // Enum attribute
          Attribute::AttrKind Kind;
          if (ParseAttrKind(Record[++i], &Kind))
            return true;

          B.addAttribute(Kind);
        } else if (Record[i] == 1) { // Align attribute
          Attribute::AttrKind Kind;
          if (ParseAttrKind(Record[++i], &Kind))
            return true;
          if (Kind == Attribute::Alignment)
            B.addAlignmentAttr(Record[++i]);
          else
            B.addStackAlignmentAttr(Record[++i]);
        } else {                     // String attribute
          assert((Record[i] == 3 || Record[i] == 4) &&
                 "Invalid attribute group entry");
          bool HasValue = (Record[i++] == 4);
          SmallString<64> KindStr;
          SmallString<64> ValStr;

          while (Record[i] != 0 && i != e)
            KindStr += Record[i++];
          assert(Record[i] == 0 && "Kind string not null terminated");

          if (HasValue) {
            // Has a value associated with it.
            ++i; // Skip the '0' that terminates the "kind" string.
            while (Record[i] != 0 && i != e)
              ValStr += Record[i++];
            assert(Record[i] == 0 && "Value string not null terminated");
          }

          B.addAttribute(KindStr.str(), ValStr.str());
        }
      }

      MAttributeGroups[GrpID] = AttributeSet::get(Context, Idx, B);
      break;
    }
    }
  }
}

bool BitcodeReader::ParseTypeTable() {
  if (Stream.EnterSubBlock(bitc::TYPE_BLOCK_ID_NEW))
    return Error("Malformed block record");

  return ParseTypeTableBody();
}

bool BitcodeReader::ParseTypeTableBody() {
  if (!TypeList.empty())
    return Error("Multiple TYPE_BLOCKs found!");

  SmallVector<uint64_t, 64> Record;
  unsigned NumRecords = 0;

  SmallString<64> TypeName;

  // Read all the records for this type table.
  while (1) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      Error("Error in the type table block");
      return true;
    case BitstreamEntry::EndBlock:
      if (NumRecords != TypeList.size())
        return Error("Invalid type forward reference in TYPE_BLOCK");
      return false;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    Type *ResultTy = 0;
    switch (Stream.readRecord(Entry.ID, Record)) {
    default: return Error("unknown type in type table");
    case bitc::TYPE_CODE_NUMENTRY: // TYPE_CODE_NUMENTRY: [numentries]
      // TYPE_CODE_NUMENTRY contains a count of the number of types in the
      // type list.  This allows us to reserve space.
      if (Record.size() < 1)
        return Error("Invalid TYPE_CODE_NUMENTRY record");
      TypeList.resize(Record[0]);
      continue;
    case bitc::TYPE_CODE_VOID:      // VOID
      ResultTy = Type::getVoidTy(Context);
      break;
    case bitc::TYPE_CODE_HALF:     // HALF
      ResultTy = Type::getHalfTy(Context);
      break;
    case bitc::TYPE_CODE_FLOAT:     // FLOAT
      ResultTy = Type::getFloatTy(Context);
      break;
    case bitc::TYPE_CODE_DOUBLE:    // DOUBLE
      ResultTy = Type::getDoubleTy(Context);
      break;
    case bitc::TYPE_CODE_X86_FP80:  // X86_FP80
      ResultTy = Type::getX86_FP80Ty(Context);
      break;
    case bitc::TYPE_CODE_FP128:     // FP128
      ResultTy = Type::getFP128Ty(Context);
      break;
    case bitc::TYPE_CODE_PPC_FP128: // PPC_FP128
      ResultTy = Type::getPPC_FP128Ty(Context);
      break;
    case bitc::TYPE_CODE_LABEL:     // LABEL
      ResultTy = Type::getLabelTy(Context);
      break;
    case bitc::TYPE_CODE_METADATA:  // METADATA
      ResultTy = Type::getMetadataTy(Context);
      break;
    case bitc::TYPE_CODE_X86_MMX:   // X86_MMX
      ResultTy = Type::getX86_MMXTy(Context);
      break;
    case bitc::TYPE_CODE_INTEGER:   // INTEGER: [width]
      if (Record.size() < 1)
        return Error("Invalid Integer type record");

      ResultTy = IntegerType::get(Context, Record[0]);
      break;
    case bitc::TYPE_CODE_POINTER: { // POINTER: [pointee type] or
                                    //          [pointee type, address space]
      if (Record.size() < 1)
        return Error("Invalid POINTER type record");
      unsigned AddressSpace = 0;
      if (Record.size() == 2)
        AddressSpace = Record[1];
      ResultTy = getTypeByID(Record[0]);
      if (ResultTy == 0) return Error("invalid element type in pointer type");
      ResultTy = PointerType::get(ResultTy, AddressSpace);
      break;
    }
    case bitc::TYPE_CODE_FUNCTION_OLD: {
      // FIXME: attrid is dead, remove it in LLVM 4.0
      // FUNCTION: [vararg, attrid, retty, paramty x N]
      if (Record.size() < 3)
        return Error("Invalid FUNCTION type record");
      SmallVector<Type*, 8> ArgTys;
      for (unsigned i = 3, e = Record.size(); i != e; ++i) {
        if (Type *T = getTypeByID(Record[i]))
          ArgTys.push_back(T);
        else
          break;
      }

      ResultTy = getTypeByID(Record[2]);
      if (ResultTy == 0 || ArgTys.size() < Record.size()-3)
        return Error("invalid type in function type");

      ResultTy = FunctionType::get(ResultTy, ArgTys, Record[0]);
      break;
    }
    case bitc::TYPE_CODE_FUNCTION: {
      // FUNCTION: [vararg, retty, paramty x N]
      if (Record.size() < 2)
        return Error("Invalid FUNCTION type record");
      SmallVector<Type*, 8> ArgTys;
      for (unsigned i = 2, e = Record.size(); i != e; ++i) {
        if (Type *T = getTypeByID(Record[i]))
          ArgTys.push_back(T);
        else
          break;
      }

      ResultTy = getTypeByID(Record[1]);
      if (ResultTy == 0 || ArgTys.size() < Record.size()-2)
        return Error("invalid type in function type");

      ResultTy = FunctionType::get(ResultTy, ArgTys, Record[0]);
      break;
    }
    case bitc::TYPE_CODE_STRUCT_ANON: {  // STRUCT: [ispacked, eltty x N]
      if (Record.size() < 1)
        return Error("Invalid STRUCT type record");
      SmallVector<Type*, 8> EltTys;
      for (unsigned i = 1, e = Record.size(); i != e; ++i) {
        if (Type *T = getTypeByID(Record[i]))
          EltTys.push_back(T);
        else
          break;
      }
      if (EltTys.size() != Record.size()-1)
        return Error("invalid type in struct type");
      ResultTy = StructType::get(Context, EltTys, Record[0]);
      break;
    }
    case bitc::TYPE_CODE_STRUCT_NAME:   // STRUCT_NAME: [strchr x N]
      if (ConvertToString(Record, 0, TypeName))
        return Error("Invalid STRUCT_NAME record");
      continue;

    case bitc::TYPE_CODE_STRUCT_NAMED: { // STRUCT: [ispacked, eltty x N]
      if (Record.size() < 1)
        return Error("Invalid STRUCT type record");

      if (NumRecords >= TypeList.size())
        return Error("invalid TYPE table");

      // Check to see if this was forward referenced, if so fill in the temp.
      StructType *Res = cast_or_null<StructType>(TypeList[NumRecords]);
      if (Res) {
        Res->setName(TypeName);
        TypeList[NumRecords] = 0;
      } else  // Otherwise, create a new struct.
        Res = StructType::create(Context, TypeName);
      TypeName.clear();

      SmallVector<Type*, 8> EltTys;
      for (unsigned i = 1, e = Record.size(); i != e; ++i) {
        if (Type *T = getTypeByID(Record[i]))
          EltTys.push_back(T);
        else
          break;
      }
      if (EltTys.size() != Record.size()-1)
        return Error("invalid STRUCT type record");
      Res->setBody(EltTys, Record[0]);
      ResultTy = Res;
      break;
    }
    case bitc::TYPE_CODE_OPAQUE: {       // OPAQUE: []
      if (Record.size() != 1)
        return Error("Invalid OPAQUE type record");

      if (NumRecords >= TypeList.size())
        return Error("invalid TYPE table");

      // Check to see if this was forward referenced, if so fill in the temp.
      StructType *Res = cast_or_null<StructType>(TypeList[NumRecords]);
      if (Res) {
        Res->setName(TypeName);
        TypeList[NumRecords] = 0;
      } else  // Otherwise, create a new struct with no body.
        Res = StructType::create(Context, TypeName);
      TypeName.clear();
      ResultTy = Res;
      break;
    }
    case bitc::TYPE_CODE_ARRAY:     // ARRAY: [numelts, eltty]
      if (Record.size() < 2)
        return Error("Invalid ARRAY type record");
      if ((ResultTy = getTypeByID(Record[1])))
        ResultTy = ArrayType::get(ResultTy, Record[0]);
      else
        return Error("Invalid ARRAY type element");
      break;
    case bitc::TYPE_CODE_VECTOR:    // VECTOR: [numelts, eltty]
      if (Record.size() < 2)
        return Error("Invalid VECTOR type record");
      if ((ResultTy = getTypeByID(Record[1])))
        ResultTy = VectorType::get(ResultTy, Record[0]);
      else
        return Error("Invalid ARRAY type element");
      break;
    }

    if (NumRecords >= TypeList.size())
      return Error("invalid TYPE table");
    assert(ResultTy && "Didn't read a type?");
    assert(TypeList[NumRecords] == 0 && "Already read type?");
    TypeList[NumRecords++] = ResultTy;
  }
}

bool BitcodeReader::ParseValueSymbolTable() {
  if (Stream.EnterSubBlock(bitc::VALUE_SYMTAB_BLOCK_ID))
    return Error("Malformed block record");

  SmallVector<uint64_t, 64> Record;

  // Read all the records for this value table.
  SmallString<128> ValueName;
  while (1) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return Error("malformed value symbol table block");
    case BitstreamEntry::EndBlock:
      return false;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: unknown type.
      break;
    case bitc::VST_CODE_ENTRY: {  // VST_ENTRY: [valueid, namechar x N]
      if (ConvertToString(Record, 1, ValueName))
        return Error("Invalid VST_ENTRY record");
      unsigned ValueID = Record[0];
      if (ValueID >= ValueList.size())
        return Error("Invalid Value ID in VST_ENTRY record");
      Value *V = ValueList[ValueID];

      V->setName(StringRef(ValueName.data(), ValueName.size()));
      ValueName.clear();
      break;
    }
    case bitc::VST_CODE_BBENTRY: {
      if (ConvertToString(Record, 1, ValueName))
        return Error("Invalid VST_BBENTRY record");
      BasicBlock *BB = getBasicBlock(Record[0]);
      if (BB == 0)
        return Error("Invalid BB ID in VST_BBENTRY record");

      BB->setName(StringRef(ValueName.data(), ValueName.size()));
      ValueName.clear();
      break;
    }
    }
  }
}

bool BitcodeReader::ParseMetadata() {
  unsigned NextMDValueNo = MDValueList.size();

  if (Stream.EnterSubBlock(bitc::METADATA_BLOCK_ID))
    return Error("Malformed block record");

  SmallVector<uint64_t, 64> Record;

  // Read all the records.
  while (1) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      Error("malformed metadata block");
      return true;
    case BitstreamEntry::EndBlock:
      return false;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    bool IsFunctionLocal = false;
    // Read a record.
    Record.clear();
    unsigned Code = Stream.readRecord(Entry.ID, Record);
    switch (Code) {
    default:  // Default behavior: ignore.
      break;
    case bitc::METADATA_NAME: {
      // Read name of the named metadata.
      SmallString<8> Name(Record.begin(), Record.end());
      Record.clear();
      Code = Stream.ReadCode();

      // METADATA_NAME is always followed by METADATA_NAMED_NODE.
      unsigned NextBitCode = Stream.readRecord(Code, Record);
      assert(NextBitCode == bitc::METADATA_NAMED_NODE); (void)NextBitCode;

      // Read named metadata elements.
      unsigned Size = Record.size();
      NamedMDNode *NMD = TheModule->getOrInsertNamedMetadata(Name);
      for (unsigned i = 0; i != Size; ++i) {
        MDNode *MD = dyn_cast<MDNode>(MDValueList.getValueFwdRef(Record[i]));
        if (MD == 0)
          return Error("Malformed metadata record");
        NMD->addOperand(MD);
      }
      break;
    }
    case bitc::METADATA_FN_NODE:
      IsFunctionLocal = true;
      // fall-through
    case bitc::METADATA_NODE: {
      if (Record.size() % 2 == 1)
        return Error("Invalid METADATA_NODE record");

      unsigned Size = Record.size();
      SmallVector<Value*, 8> Elts;
      for (unsigned i = 0; i != Size; i += 2) {
        Type *Ty = getTypeByID(Record[i]);
        if (!Ty) return Error("Invalid METADATA_NODE record");
        if (Ty->isMetadataTy())
          Elts.push_back(MDValueList.getValueFwdRef(Record[i+1]));
        else if (!Ty->isVoidTy())
          Elts.push_back(ValueList.getValueFwdRef(Record[i+1], Ty));
        else
          Elts.push_back(NULL);
      }
      Value *V = MDNode::getWhenValsUnresolved(Context, Elts, IsFunctionLocal);
      IsFunctionLocal = false;
      MDValueList.AssignValue(V, NextMDValueNo++);
      break;
    }
    case bitc::METADATA_STRING: {
      SmallString<8> String(Record.begin(), Record.end());
      Value *V = MDString::get(Context, String);
      MDValueList.AssignValue(V, NextMDValueNo++);
      break;
    }
    case bitc::METADATA_KIND: {
      if (Record.size() < 2)
        return Error("Invalid METADATA_KIND record");

      unsigned Kind = Record[0];
      SmallString<8> Name(Record.begin()+1, Record.end());

      unsigned NewKind = TheModule->getMDKindID(Name.str());
      if (!MDKindMap.insert(std::make_pair(Kind, NewKind)).second)
        return Error("Conflicting METADATA_KIND records");
      break;
    }
    }
  }
}

/// decodeSignRotatedValue - Decode a signed value stored with the sign bit in
/// the LSB for dense VBR encoding.
uint64_t BitcodeReader::decodeSignRotatedValue(uint64_t V) {
  if ((V & 1) == 0)
    return V >> 1;
  if (V != 1)
    return -(V >> 1);
  // There is no such thing as -0 with integers.  "-0" really means MININT.
  return 1ULL << 63;
}

/// ResolveGlobalAndAliasInits - Resolve all of the initializers for global
/// values and aliases that we can.
bool BitcodeReader::ResolveGlobalAndAliasInits() {
  std::vector<std::pair<GlobalVariable*, unsigned> > GlobalInitWorklist;
  std::vector<std::pair<GlobalAlias*, unsigned> > AliasInitWorklist;
  std::vector<std::pair<Function*, unsigned> > FunctionPrefixWorklist;

  GlobalInitWorklist.swap(GlobalInits);
  AliasInitWorklist.swap(AliasInits);
  FunctionPrefixWorklist.swap(FunctionPrefixes);

  while (!GlobalInitWorklist.empty()) {
    unsigned ValID = GlobalInitWorklist.back().second;
    if (ValID >= ValueList.size()) {
      // Not ready to resolve this yet, it requires something later in the file.
      GlobalInits.push_back(GlobalInitWorklist.back());
    } else {
      if (Constant *C = dyn_cast<Constant>(ValueList[ValID]))
        GlobalInitWorklist.back().first->setInitializer(C);
      else
        return Error("Global variable initializer is not a constant!");
    }
    GlobalInitWorklist.pop_back();
  }

  while (!AliasInitWorklist.empty()) {
    unsigned ValID = AliasInitWorklist.back().second;
    if (ValID >= ValueList.size()) {
      AliasInits.push_back(AliasInitWorklist.back());
    } else {
      if (Constant *C = dyn_cast<Constant>(ValueList[ValID]))
        AliasInitWorklist.back().first->setAliasee(C);
      else
        return Error("Alias initializer is not a constant!");
    }
    AliasInitWorklist.pop_back();
  }

  while (!FunctionPrefixWorklist.empty()) {
    unsigned ValID = FunctionPrefixWorklist.back().second;
    if (ValID >= ValueList.size()) {
      FunctionPrefixes.push_back(FunctionPrefixWorklist.back());
    } else {
      if (Constant *C = dyn_cast<Constant>(ValueList[ValID]))
        FunctionPrefixWorklist.back().first->setPrefixData(C);
      else
        return Error("Function prefix is not a constant!");
    }
    FunctionPrefixWorklist.pop_back();
  }

  return false;
}

static APInt ReadWideAPInt(ArrayRef<uint64_t> Vals, unsigned TypeBits) {
  SmallVector<uint64_t, 8> Words(Vals.size());
  std::transform(Vals.begin(), Vals.end(), Words.begin(),
                 BitcodeReader::decodeSignRotatedValue);

  return APInt(TypeBits, Words);
}

bool BitcodeReader::ParseConstants() {
  if (Stream.EnterSubBlock(bitc::CONSTANTS_BLOCK_ID))
    return Error("Malformed block record");

  SmallVector<uint64_t, 64> Record;

  // Read all the records for this value table.
  Type *CurTy = Type::getInt32Ty(Context);
  unsigned NextCstNo = ValueList.size();
  while (1) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return Error("malformed block record in AST file");
    case BitstreamEntry::EndBlock:
      if (NextCstNo != ValueList.size())
        return Error("Invalid constant reference!");

      // Once all the constants have been read, go through and resolve forward
      // references.
      ValueList.ResolveConstantForwardRefs();
      return false;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    Value *V = 0;
    unsigned BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default:  // Default behavior: unknown constant
    case bitc::CST_CODE_UNDEF:     // UNDEF
      V = UndefValue::get(CurTy);
      break;
    case bitc::CST_CODE_SETTYPE:   // SETTYPE: [typeid]
      if (Record.empty())
        return Error("Malformed CST_SETTYPE record");
      if (Record[0] >= TypeList.size())
        return Error("Invalid Type ID in CST_SETTYPE record");
      CurTy = TypeList[Record[0]];
      continue;  // Skip the ValueList manipulation.
    case bitc::CST_CODE_NULL:      // NULL
      V = Constant::getNullValue(CurTy);
      break;
    case bitc::CST_CODE_INTEGER:   // INTEGER: [intval]
      if (!CurTy->isIntegerTy() || Record.empty())
        return Error("Invalid CST_INTEGER record");
      V = ConstantInt::get(CurTy, decodeSignRotatedValue(Record[0]));
      break;
    case bitc::CST_CODE_WIDE_INTEGER: {// WIDE_INTEGER: [n x intval]
      if (!CurTy->isIntegerTy() || Record.empty())
        return Error("Invalid WIDE_INTEGER record");

      APInt VInt = ReadWideAPInt(Record,
                                 cast<IntegerType>(CurTy)->getBitWidth());
      V = ConstantInt::get(Context, VInt);

      break;
    }
    case bitc::CST_CODE_FLOAT: {    // FLOAT: [fpval]
      if (Record.empty())
        return Error("Invalid FLOAT record");
      if (CurTy->isHalfTy())
        V = ConstantFP::get(Context, APFloat(APFloat::IEEEhalf,
                                             APInt(16, (uint16_t)Record[0])));
      else if (CurTy->isFloatTy())
        V = ConstantFP::get(Context, APFloat(APFloat::IEEEsingle,
                                             APInt(32, (uint32_t)Record[0])));
      else if (CurTy->isDoubleTy())
        V = ConstantFP::get(Context, APFloat(APFloat::IEEEdouble,
                                             APInt(64, Record[0])));
      else if (CurTy->isX86_FP80Ty()) {
        // Bits are not stored the same way as a normal i80 APInt, compensate.
        uint64_t Rearrange[2];
        Rearrange[0] = (Record[1] & 0xffffLL) | (Record[0] << 16);
        Rearrange[1] = Record[0] >> 48;
        V = ConstantFP::get(Context, APFloat(APFloat::x87DoubleExtended,
                                             APInt(80, Rearrange)));
      } else if (CurTy->isFP128Ty())
        V = ConstantFP::get(Context, APFloat(APFloat::IEEEquad,
                                             APInt(128, Record)));
      else if (CurTy->isPPC_FP128Ty())
        V = ConstantFP::get(Context, APFloat(APFloat::PPCDoubleDouble,
                                             APInt(128, Record)));
      else
        V = UndefValue::get(CurTy);
      break;
    }

    case bitc::CST_CODE_AGGREGATE: {// AGGREGATE: [n x value number]
      if (Record.empty())
        return Error("Invalid CST_AGGREGATE record");

      unsigned Size = Record.size();
      SmallVector<Constant*, 16> Elts;

      if (StructType *STy = dyn_cast<StructType>(CurTy)) {
        for (unsigned i = 0; i != Size; ++i)
          Elts.push_back(ValueList.getConstantFwdRef(Record[i],
                                                     STy->getElementType(i)));
        V = ConstantStruct::get(STy, Elts);
      } else if (ArrayType *ATy = dyn_cast<ArrayType>(CurTy)) {
        Type *EltTy = ATy->getElementType();
        for (unsigned i = 0; i != Size; ++i)
          Elts.push_back(ValueList.getConstantFwdRef(Record[i], EltTy));
        V = ConstantArray::get(ATy, Elts);
      } else if (VectorType *VTy = dyn_cast<VectorType>(CurTy)) {
        Type *EltTy = VTy->getElementType();
        for (unsigned i = 0; i != Size; ++i)
          Elts.push_back(ValueList.getConstantFwdRef(Record[i], EltTy));
        V = ConstantVector::get(Elts);
      } else {
        V = UndefValue::get(CurTy);
      }
      break;
    }
    case bitc::CST_CODE_STRING:    // STRING: [values]
    case bitc::CST_CODE_CSTRING: { // CSTRING: [values]
      if (Record.empty())
        return Error("Invalid CST_STRING record");

      SmallString<16> Elts(Record.begin(), Record.end());
      V = ConstantDataArray::getString(Context, Elts,
                                       BitCode == bitc::CST_CODE_CSTRING);
      break;
    }
    case bitc::CST_CODE_DATA: {// DATA: [n x value]
      if (Record.empty())
        return Error("Invalid CST_DATA record");

      Type *EltTy = cast<SequentialType>(CurTy)->getElementType();
      unsigned Size = Record.size();

      if (EltTy->isIntegerTy(8)) {
        SmallVector<uint8_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isIntegerTy(16)) {
        SmallVector<uint16_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isIntegerTy(32)) {
        SmallVector<uint32_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isIntegerTy(64)) {
        SmallVector<uint64_t, 16> Elts(Record.begin(), Record.end());
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isFloatTy()) {
        SmallVector<float, 16> Elts(Size);
        std::transform(Record.begin(), Record.end(), Elts.begin(), BitsToFloat);
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else if (EltTy->isDoubleTy()) {
        SmallVector<double, 16> Elts(Size);
        std::transform(Record.begin(), Record.end(), Elts.begin(),
                       BitsToDouble);
        if (isa<VectorType>(CurTy))
          V = ConstantDataVector::get(Context, Elts);
        else
          V = ConstantDataArray::get(Context, Elts);
      } else {
        return Error("Unknown element type in CE_DATA");
      }
      break;
    }

    case bitc::CST_CODE_CE_BINOP: {  // CE_BINOP: [opcode, opval, opval]
      if (Record.size() < 3) return Error("Invalid CE_BINOP record");
      int Opc = GetDecodedBinaryOpcode(Record[0], CurTy);
      if (Opc < 0) {
        V = UndefValue::get(CurTy);  // Unknown binop.
      } else {
        Constant *LHS = ValueList.getConstantFwdRef(Record[1], CurTy);
        Constant *RHS = ValueList.getConstantFwdRef(Record[2], CurTy);
        unsigned Flags = 0;
        if (Record.size() >= 4) {
          if (Opc == Instruction::Add ||
              Opc == Instruction::Sub ||
              Opc == Instruction::Mul ||
              Opc == Instruction::Shl) {
            if (Record[3] & (1 << bitc::OBO_NO_SIGNED_WRAP))
              Flags |= OverflowingBinaryOperator::NoSignedWrap;
            if (Record[3] & (1 << bitc::OBO_NO_UNSIGNED_WRAP))
              Flags |= OverflowingBinaryOperator::NoUnsignedWrap;
          } else if (Opc == Instruction::SDiv ||
                     Opc == Instruction::UDiv ||
                     Opc == Instruction::LShr ||
                     Opc == Instruction::AShr) {
            if (Record[3] & (1 << bitc::PEO_EXACT))
              Flags |= SDivOperator::IsExact;
          }
        }
        V = ConstantExpr::get(Opc, LHS, RHS, Flags);
      }
      break;
    }
    case bitc::CST_CODE_CE_CAST: {  // CE_CAST: [opcode, opty, opval]
      if (Record.size() < 3) return Error("Invalid CE_CAST record");
      int Opc = GetDecodedCastOpcode(Record[0]);
      if (Opc < 0) {
        V = UndefValue::get(CurTy);  // Unknown cast.
      } else {
        Type *OpTy = getTypeByID(Record[1]);
        if (!OpTy) return Error("Invalid CE_CAST record");
        Constant *Op = ValueList.getConstantFwdRef(Record[2], OpTy);
        V = ConstantExpr::getCast(Opc, Op, CurTy);
      }
      break;
    }
    case bitc::CST_CODE_CE_INBOUNDS_GEP:
    case bitc::CST_CODE_CE_GEP: {  // CE_GEP:        [n x operands]
      if (Record.size() & 1) return Error("Invalid CE_GEP record");
      SmallVector<Constant*, 16> Elts;
      for (unsigned i = 0, e = Record.size(); i != e; i += 2) {
        Type *ElTy = getTypeByID(Record[i]);
        if (!ElTy) return Error("Invalid CE_GEP record");
        Elts.push_back(ValueList.getConstantFwdRef(Record[i+1], ElTy));
      }
      ArrayRef<Constant *> Indices(Elts.begin() + 1, Elts.end());
      V = ConstantExpr::getGetElementPtr(Elts[0], Indices,
                                         BitCode ==
                                           bitc::CST_CODE_CE_INBOUNDS_GEP);
      break;
    }
    case bitc::CST_CODE_CE_SELECT: {  // CE_SELECT: [opval#, opval#, opval#]
      if (Record.size() < 3) return Error("Invalid CE_SELECT record");

      Type *SelectorTy = Type::getInt1Ty(Context);

      // If CurTy is a vector of length n, then Record[0] must be a <n x i1>
      // vector. Otherwise, it must be a single bit.
      if (VectorType *VTy = dyn_cast<VectorType>(CurTy))
        SelectorTy = VectorType::get(Type::getInt1Ty(Context),
                                     VTy->getNumElements());

      V = ConstantExpr::getSelect(ValueList.getConstantFwdRef(Record[0],
                                                              SelectorTy),
                                  ValueList.getConstantFwdRef(Record[1],CurTy),
                                  ValueList.getConstantFwdRef(Record[2],CurTy));
      break;
    }
    case bitc::CST_CODE_CE_EXTRACTELT: { // CE_EXTRACTELT: [opty, opval, opval]
      if (Record.size() < 3) return Error("Invalid CE_EXTRACTELT record");
      VectorType *OpTy =
        dyn_cast_or_null<VectorType>(getTypeByID(Record[0]));
      if (OpTy == 0) return Error("Invalid CE_EXTRACTELT record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[1], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[2],
                                                  Type::getInt32Ty(Context));
      V = ConstantExpr::getExtractElement(Op0, Op1);
      break;
    }
    case bitc::CST_CODE_CE_INSERTELT: { // CE_INSERTELT: [opval, opval, opval]
      VectorType *OpTy = dyn_cast<VectorType>(CurTy);
      if (Record.size() < 3 || OpTy == 0)
        return Error("Invalid CE_INSERTELT record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[0], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[1],
                                                  OpTy->getElementType());
      Constant *Op2 = ValueList.getConstantFwdRef(Record[2],
                                                  Type::getInt32Ty(Context));
      V = ConstantExpr::getInsertElement(Op0, Op1, Op2);
      break;
    }
    case bitc::CST_CODE_CE_SHUFFLEVEC: { // CE_SHUFFLEVEC: [opval, opval, opval]
      VectorType *OpTy = dyn_cast<VectorType>(CurTy);
      if (Record.size() < 3 || OpTy == 0)
        return Error("Invalid CE_SHUFFLEVEC record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[0], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[1], OpTy);
      Type *ShufTy = VectorType::get(Type::getInt32Ty(Context),
                                                 OpTy->getNumElements());
      Constant *Op2 = ValueList.getConstantFwdRef(Record[2], ShufTy);
      V = ConstantExpr::getShuffleVector(Op0, Op1, Op2);
      break;
    }
    case bitc::CST_CODE_CE_SHUFVEC_EX: { // [opty, opval, opval, opval]
      VectorType *RTy = dyn_cast<VectorType>(CurTy);
      VectorType *OpTy =
        dyn_cast_or_null<VectorType>(getTypeByID(Record[0]));
      if (Record.size() < 4 || RTy == 0 || OpTy == 0)
        return Error("Invalid CE_SHUFVEC_EX record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[1], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[2], OpTy);
      Type *ShufTy = VectorType::get(Type::getInt32Ty(Context),
                                                 RTy->getNumElements());
      Constant *Op2 = ValueList.getConstantFwdRef(Record[3], ShufTy);
      V = ConstantExpr::getShuffleVector(Op0, Op1, Op2);
      break;
    }
    case bitc::CST_CODE_CE_CMP: {     // CE_CMP: [opty, opval, opval, pred]
      if (Record.size() < 4) return Error("Invalid CE_CMP record");
      Type *OpTy = getTypeByID(Record[0]);
      if (OpTy == 0) return Error("Invalid CE_CMP record");
      Constant *Op0 = ValueList.getConstantFwdRef(Record[1], OpTy);
      Constant *Op1 = ValueList.getConstantFwdRef(Record[2], OpTy);

      if (OpTy->isFPOrFPVectorTy())
        V = ConstantExpr::getFCmp(Record[3], Op0, Op1);
      else
        V = ConstantExpr::getICmp(Record[3], Op0, Op1);
      break;
    }
    // This maintains backward compatibility, pre-asm dialect keywords.
    // FIXME: Remove with the 4.0 release.
    case bitc::CST_CODE_INLINEASM_OLD: {
      if (Record.size() < 2) return Error("Invalid INLINEASM record");
      std::string AsmStr, ConstrStr;
      bool HasSideEffects = Record[0] & 1;
      bool IsAlignStack = Record[0] >> 1;
      unsigned AsmStrSize = Record[1];
      if (2+AsmStrSize >= Record.size())
        return Error("Invalid INLINEASM record");
      unsigned ConstStrSize = Record[2+AsmStrSize];
      if (3+AsmStrSize+ConstStrSize > Record.size())
        return Error("Invalid INLINEASM record");

      for (unsigned i = 0; i != AsmStrSize; ++i)
        AsmStr += (char)Record[2+i];
      for (unsigned i = 0; i != ConstStrSize; ++i)
        ConstrStr += (char)Record[3+AsmStrSize+i];
      PointerType *PTy = cast<PointerType>(CurTy);
      V = InlineAsm::get(cast<FunctionType>(PTy->getElementType()),
                         AsmStr, ConstrStr, HasSideEffects, IsAlignStack);
      break;
    }
    // This version adds support for the asm dialect keywords (e.g.,
    // inteldialect).
    case bitc::CST_CODE_INLINEASM: {
      if (Record.size() < 2) return Error("Invalid INLINEASM record");
      std::string AsmStr, ConstrStr;
      bool HasSideEffects = Record[0] & 1;
      bool IsAlignStack = (Record[0] >> 1) & 1;
      unsigned AsmDialect = Record[0] >> 2;
      unsigned AsmStrSize = Record[1];
      if (2+AsmStrSize >= Record.size())
        return Error("Invalid INLINEASM record");
      unsigned ConstStrSize = Record[2+AsmStrSize];
      if (3+AsmStrSize+ConstStrSize > Record.size())
        return Error("Invalid INLINEASM record");

      for (unsigned i = 0; i != AsmStrSize; ++i)
        AsmStr += (char)Record[2+i];
      for (unsigned i = 0; i != ConstStrSize; ++i)
        ConstrStr += (char)Record[3+AsmStrSize+i];
      PointerType *PTy = cast<PointerType>(CurTy);
      V = InlineAsm::get(cast<FunctionType>(PTy->getElementType()),
                         AsmStr, ConstrStr, HasSideEffects, IsAlignStack,
                         InlineAsm::AsmDialect(AsmDialect));
      break;
    }
    case bitc::CST_CODE_BLOCKADDRESS:{
      if (Record.size() < 3) return Error("Invalid CE_BLOCKADDRESS record");
      Type *FnTy = getTypeByID(Record[0]);
      if (FnTy == 0) return Error("Invalid CE_BLOCKADDRESS record");
      Function *Fn =
        dyn_cast_or_null<Function>(ValueList.getConstantFwdRef(Record[1],FnTy));
      if (Fn == 0) return Error("Invalid CE_BLOCKADDRESS record");

      // If the function is already parsed we can insert the block address right
      // away.
      if (!Fn->empty()) {
        Function::iterator BBI = Fn->begin(), BBE = Fn->end();
        for (size_t I = 0, E = Record[2]; I != E; ++I) {
          if (BBI == BBE)
            return Error("Invalid blockaddress block #");
          ++BBI;
        }
        V = BlockAddress::get(Fn, BBI);
      } else {
        // Otherwise insert a placeholder and remember it so it can be inserted
        // when the function is parsed.
        GlobalVariable *FwdRef = new GlobalVariable(*Fn->getParent(),
                                                    Type::getInt8Ty(Context),
                                            false, GlobalValue::InternalLinkage,
                                                    0, "");
        BlockAddrFwdRefs[Fn].push_back(std::make_pair(Record[2], FwdRef));
        V = FwdRef;
      }
      break;
    }
    }

    ValueList.AssignValue(V, NextCstNo);
    ++NextCstNo;
  }
}

bool BitcodeReader::ParseUseLists() {
  if (Stream.EnterSubBlock(bitc::USELIST_BLOCK_ID))
    return Error("Malformed block record");

  SmallVector<uint64_t, 64> Record;

  // Read all the records.
  while (1) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return Error("malformed use list block");
    case BitstreamEntry::EndBlock:
      return false;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a use list record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: unknown type.
      break;
    case bitc::USELIST_CODE_ENTRY: { // USELIST_CODE_ENTRY: TBD.
      unsigned RecordLength = Record.size();
      if (RecordLength < 1)
        return Error ("Invalid UseList reader!");
      UseListRecords.push_back(Record);
      break;
    }
    }
  }
}

/// RememberAndSkipFunctionBody - When we see the block for a function body,
/// remember where it is and then skip it.  This lets us lazily deserialize the
/// functions.
bool BitcodeReader::RememberAndSkipFunctionBody() {
  // Get the function we are talking about.
  if (FunctionsWithBodies.empty())
    return Error("Insufficient function protos");

  Function *Fn = FunctionsWithBodies.back();
  FunctionsWithBodies.pop_back();

  // Save the current stream state.
  uint64_t CurBit = Stream.GetCurrentBitNo();
  DeferredFunctionInfo[Fn] = CurBit;

  // Skip over the function block for now.
  if (Stream.SkipBlock())
    return Error("Malformed block record");
  return false;
}

bool BitcodeReader::GlobalCleanup() {
  // Patch the initializers for globals and aliases up.
  ResolveGlobalAndAliasInits();
  if (!GlobalInits.empty() || !AliasInits.empty())
    return Error("Malformed global initializer set");

  // Look for intrinsic functions which need to be upgraded at some point
  for (Module::iterator FI = TheModule->begin(), FE = TheModule->end();
       FI != FE; ++FI) {
    Function *NewFn;
    if (UpgradeIntrinsicFunction(FI, NewFn))
      UpgradedIntrinsics.push_back(std::make_pair(FI, NewFn));
  }

  // Look for global variables which need to be renamed.
  for (Module::global_iterator
         GI = TheModule->global_begin(), GE = TheModule->global_end();
       GI != GE; ++GI)
    UpgradeGlobalVariable(GI);
  // Force deallocation of memory for these vectors to favor the client that
  // want lazy deserialization.
  std::vector<std::pair<GlobalVariable*, unsigned> >().swap(GlobalInits);
  std::vector<std::pair<GlobalAlias*, unsigned> >().swap(AliasInits);
  return false;
}

bool BitcodeReader::ParseModule(bool Resume) {
  if (Resume)
    Stream.JumpToBit(NextUnreadBit);
  else if (Stream.EnterSubBlock(bitc::MODULE_BLOCK_ID))
    return Error("Malformed block record");

  SmallVector<uint64_t, 64> Record;
  std::vector<std::string> SectionTable;
  std::vector<std::string> GCTable;

  // Read all the records for this module.
  while (1) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      Error("malformed module block");
      return true;
    case BitstreamEntry::EndBlock:
      return GlobalCleanup();

    case BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      default:  // Skip unknown content.
        if (Stream.SkipBlock())
          return Error("Malformed block record");
        break;
      case bitc::BLOCKINFO_BLOCK_ID:
        if (Stream.ReadBlockInfoBlock())
          return Error("Malformed BlockInfoBlock");
        break;
      case bitc::PARAMATTR_BLOCK_ID:
        if (ParseAttributeBlock())
          return true;
        break;
      case bitc::PARAMATTR_GROUP_BLOCK_ID:
        if (ParseAttributeGroupBlock())
          return true;
        break;
      case bitc::TYPE_BLOCK_ID_NEW:
        if (ParseTypeTable())
          return true;
        break;
      case bitc::VALUE_SYMTAB_BLOCK_ID:
        if (ParseValueSymbolTable())
          return true;
        SeenValueSymbolTable = true;
        break;
      case bitc::CONSTANTS_BLOCK_ID:
        if (ParseConstants() || ResolveGlobalAndAliasInits())
          return true;
        break;
      case bitc::METADATA_BLOCK_ID:
        if (ParseMetadata())
          return true;
        break;
      case bitc::FUNCTION_BLOCK_ID:
        // If this is the first function body we've seen, reverse the
        // FunctionsWithBodies list.
        if (!SeenFirstFunctionBody) {
          std::reverse(FunctionsWithBodies.begin(), FunctionsWithBodies.end());
          if (GlobalCleanup())
            return true;
          SeenFirstFunctionBody = true;
        }

        if (RememberAndSkipFunctionBody())
          return true;
        // For streaming bitcode, suspend parsing when we reach the function
        // bodies. Subsequent materialization calls will resume it when
        // necessary. For streaming, the function bodies must be at the end of
        // the bitcode. If the bitcode file is old, the symbol table will be
        // at the end instead and will not have been seen yet. In this case,
        // just finish the parse now.
        if (LazyStreamer && SeenValueSymbolTable) {
          NextUnreadBit = Stream.GetCurrentBitNo();
          return false;
        }
        break;
      case bitc::USELIST_BLOCK_ID:
        if (ParseUseLists())
          return true;
        break;
      }
      continue;

    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }


    // Read a record.
    switch (Stream.readRecord(Entry.ID, Record)) {
    default: break;  // Default behavior, ignore unknown content.
    case bitc::MODULE_CODE_VERSION: {  // VERSION: [version#]
      if (Record.size() < 1)
        return Error("Malformed MODULE_CODE_VERSION");
      // Only version #0 and #1 are supported so far.
      unsigned module_version = Record[0];
      switch (module_version) {
        default: return Error("Unknown bitstream version!");
        case 0:
          UseRelativeIDs = false;
          break;
        case 1:
          UseRelativeIDs = true;
          break;
      }
      break;
    }
    case bitc::MODULE_CODE_TRIPLE: {  // TRIPLE: [strchr x N]
      std::string S;
      if (ConvertToString(Record, 0, S))
        return Error("Invalid MODULE_CODE_TRIPLE record");
      TheModule->setTargetTriple(S);
      break;
    }
    case bitc::MODULE_CODE_DATALAYOUT: {  // DATALAYOUT: [strchr x N]
      std::string S;
      if (ConvertToString(Record, 0, S))
        return Error("Invalid MODULE_CODE_DATALAYOUT record");
      TheModule->setDataLayout(S);
      break;
    }
    case bitc::MODULE_CODE_ASM: {  // ASM: [strchr x N]
      std::string S;
      if (ConvertToString(Record, 0, S))
        return Error("Invalid MODULE_CODE_ASM record");
      TheModule->setModuleInlineAsm(S);
      break;
    }
    case bitc::MODULE_CODE_DEPLIB: {  // DEPLIB: [strchr x N]
      // FIXME: Remove in 4.0.
      std::string S;
      if (ConvertToString(Record, 0, S))
        return Error("Invalid MODULE_CODE_DEPLIB record");
      // Ignore value.
      break;
    }
    case bitc::MODULE_CODE_SECTIONNAME: {  // SECTIONNAME: [strchr x N]
      std::string S;
      if (ConvertToString(Record, 0, S))
        return Error("Invalid MODULE_CODE_SECTIONNAME record");
      SectionTable.push_back(S);
      break;
    }
    case bitc::MODULE_CODE_GCNAME: {  // SECTIONNAME: [strchr x N]
      std::string S;
      if (ConvertToString(Record, 0, S))
        return Error("Invalid MODULE_CODE_GCNAME record");
      GCTable.push_back(S);
      break;
    }
    // GLOBALVAR: [pointer type, isconst, initid,
    //             linkage, alignment, section, visibility, threadlocal,
    //             unnamed_addr]
    case bitc::MODULE_CODE_GLOBALVAR: {
      if (Record.size() < 6)
        return Error("Invalid MODULE_CODE_GLOBALVAR record");
      Type *Ty = getTypeByID(Record[0]);
      if (!Ty) return Error("Invalid MODULE_CODE_GLOBALVAR record");
      if (!Ty->isPointerTy())
        return Error("Global not a pointer type!");
      unsigned AddressSpace = cast<PointerType>(Ty)->getAddressSpace();
      Ty = cast<PointerType>(Ty)->getElementType();

      bool isConstant = Record[1];
      GlobalValue::LinkageTypes Linkage = GetDecodedLinkage(Record[3]);
      unsigned Alignment = (1 << Record[4]) >> 1;
      std::string Section;
      if (Record[5]) {
        if (Record[5]-1 >= SectionTable.size())
          return Error("Invalid section ID");
        Section = SectionTable[Record[5]-1];
      }
      GlobalValue::VisibilityTypes Visibility = GlobalValue::DefaultVisibility;
      if (Record.size() > 6)
        Visibility = GetDecodedVisibility(Record[6]);

      GlobalVariable::ThreadLocalMode TLM = GlobalVariable::NotThreadLocal;
      if (Record.size() > 7)
        TLM = GetDecodedThreadLocalMode(Record[7]);

      bool UnnamedAddr = false;
      if (Record.size() > 8)
        UnnamedAddr = Record[8];

      bool ExternallyInitialized = false;
      if (Record.size() > 9)
        ExternallyInitialized = Record[9];

      GlobalVariable *NewGV =
        new GlobalVariable(*TheModule, Ty, isConstant, Linkage, 0, "", 0,
                           TLM, AddressSpace, ExternallyInitialized);
      NewGV->setAlignment(Alignment);
      if (!Section.empty())
        NewGV->setSection(Section);
      NewGV->setVisibility(Visibility);
      NewGV->setUnnamedAddr(UnnamedAddr);

      ValueList.push_back(NewGV);

      // Remember which value to use for the global initializer.
      if (unsigned InitID = Record[2])
        GlobalInits.push_back(std::make_pair(NewGV, InitID-1));
      break;
    }
    // FUNCTION:  [type, callingconv, isproto, linkage, paramattr,
    //             alignment, section, visibility, gc, unnamed_addr]
    case bitc::MODULE_CODE_FUNCTION: {
      if (Record.size() < 8)
        return Error("Invalid MODULE_CODE_FUNCTION record");
      Type *Ty = getTypeByID(Record[0]);
      if (!Ty) return Error("Invalid MODULE_CODE_FUNCTION record");
      if (!Ty->isPointerTy())
        return Error("Function not a pointer type!");
      FunctionType *FTy =
        dyn_cast<FunctionType>(cast<PointerType>(Ty)->getElementType());
      if (!FTy)
        return Error("Function not a pointer to function type!");

      Function *Func = Function::Create(FTy, GlobalValue::ExternalLinkage,
                                        "", TheModule);

      Func->setCallingConv(static_cast<CallingConv::ID>(Record[1]));
      bool isProto = Record[2];
      Func->setLinkage(GetDecodedLinkage(Record[3]));
      Func->setAttributes(getAttributes(Record[4]));

      Func->setAlignment((1 << Record[5]) >> 1);
      if (Record[6]) {
        if (Record[6]-1 >= SectionTable.size())
          return Error("Invalid section ID");
        Func->setSection(SectionTable[Record[6]-1]);
      }
      Func->setVisibility(GetDecodedVisibility(Record[7]));
      if (Record.size() > 8 && Record[8]) {
        if (Record[8]-1 > GCTable.size())
          return Error("Invalid GC ID");
        Func->setGC(GCTable[Record[8]-1].c_str());
      }
      bool UnnamedAddr = false;
      if (Record.size() > 9)
        UnnamedAddr = Record[9];
      Func->setUnnamedAddr(UnnamedAddr);
      if (Record.size() > 10 && Record[10] != 0)
        FunctionPrefixes.push_back(std::make_pair(Func, Record[10]-1));
      ValueList.push_back(Func);

      // If this is a function with a body, remember the prototype we are
      // creating now, so that we can match up the body with them later.
      if (!isProto) {
        FunctionsWithBodies.push_back(Func);
        if (LazyStreamer) DeferredFunctionInfo[Func] = 0;
      }
      break;
    }
    // ALIAS: [alias type, aliasee val#, linkage]
    // ALIAS: [alias type, aliasee val#, linkage, visibility]
    case bitc::MODULE_CODE_ALIAS: {
      if (Record.size() < 3)
        return Error("Invalid MODULE_ALIAS record");
      Type *Ty = getTypeByID(Record[0]);
      if (!Ty) return Error("Invalid MODULE_ALIAS record");
      if (!Ty->isPointerTy())
        return Error("Function not a pointer type!");

      GlobalAlias *NewGA = new GlobalAlias(Ty, GetDecodedLinkage(Record[2]),
                                           "", 0, TheModule);
      // Old bitcode files didn't have visibility field.
      if (Record.size() > 3)
        NewGA->setVisibility(GetDecodedVisibility(Record[3]));
      ValueList.push_back(NewGA);
      AliasInits.push_back(std::make_pair(NewGA, Record[1]));
      break;
    }
    /// MODULE_CODE_PURGEVALS: [numvals]
    case bitc::MODULE_CODE_PURGEVALS:
      // Trim down the value list to the specified size.
      if (Record.size() < 1 || Record[0] > ValueList.size())
        return Error("Invalid MODULE_PURGEVALS record");
      ValueList.shrinkTo(Record[0]);
      break;
    }
    Record.clear();
  }
}

bool BitcodeReader::ParseBitcodeInto(Module *M) {
  TheModule = 0;

  if (InitStream()) return true;

  // Sniff for the signature.
  if (Stream.Read(8) != 'B' ||
      Stream.Read(8) != 'C' ||
      Stream.Read(4) != 0x0 ||
      Stream.Read(4) != 0xC ||
      Stream.Read(4) != 0xE ||
      Stream.Read(4) != 0xD)
    return Error("Invalid bitcode signature");

  // We expect a number of well-defined blocks, though we don't necessarily
  // need to understand them all.
  while (1) {
    if (Stream.AtEndOfStream())
      return false;

    BitstreamEntry Entry =
      Stream.advance(BitstreamCursor::AF_DontAutoprocessAbbrevs);

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      Error("malformed module file");
      return true;
    case BitstreamEntry::EndBlock:
      return false;

    case BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      case bitc::BLOCKINFO_BLOCK_ID:
        if (Stream.ReadBlockInfoBlock())
          return Error("Malformed BlockInfoBlock");
        break;
      case bitc::MODULE_BLOCK_ID:
        // Reject multiple MODULE_BLOCK's in a single bitstream.
        if (TheModule)
          return Error("Multiple MODULE_BLOCKs in same stream");
        TheModule = M;
        if (ParseModule(false))
          return true;
        if (LazyStreamer) return false;
        break;
      default:
        if (Stream.SkipBlock())
          return Error("Malformed block record");
        break;
      }
      continue;
    case BitstreamEntry::Record:
      // There should be no records in the top-level of blocks.

      // The ranlib in Xcode 4 will align archive members by appending newlines
      // to the end of them. If this file size is a multiple of 4 but not 8, we
      // have to read and ignore these final 4 bytes :-(
      if (Stream.getAbbrevIDWidth() == 2 && Entry.ID == 2 &&
          Stream.Read(6) == 2 && Stream.Read(24) == 0xa0a0a &&
          Stream.AtEndOfStream())
        return false;

      return Error("Invalid record at top-level");
    }
  }
}

bool BitcodeReader::ParseModuleTriple(std::string &Triple) {
  if (Stream.EnterSubBlock(bitc::MODULE_BLOCK_ID))
    return Error("Malformed block record");

  SmallVector<uint64_t, 64> Record;

  // Read all the records for this module.
  while (1) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return Error("malformed module block");
    case BitstreamEntry::EndBlock:
      return false;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    switch (Stream.readRecord(Entry.ID, Record)) {
    default: break;  // Default behavior, ignore unknown content.
    case bitc::MODULE_CODE_TRIPLE: {  // TRIPLE: [strchr x N]
      std::string S;
      if (ConvertToString(Record, 0, S))
        return Error("Invalid MODULE_CODE_TRIPLE record");
      Triple = S;
      break;
    }
    }
    Record.clear();
  }
}

bool BitcodeReader::ParseTriple(std::string &Triple) {
  if (InitStream()) return true;

  // Sniff for the signature.
  if (Stream.Read(8) != 'B' ||
      Stream.Read(8) != 'C' ||
      Stream.Read(4) != 0x0 ||
      Stream.Read(4) != 0xC ||
      Stream.Read(4) != 0xE ||
      Stream.Read(4) != 0xD)
    return Error("Invalid bitcode signature");

  // We expect a number of well-defined blocks, though we don't necessarily
  // need to understand them all.
  while (1) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      Error("malformed module file");
      return true;
    case BitstreamEntry::EndBlock:
      return false;

    case BitstreamEntry::SubBlock:
      if (Entry.ID == bitc::MODULE_BLOCK_ID)
        return ParseModuleTriple(Triple);

      // Ignore other sub-blocks.
      if (Stream.SkipBlock()) {
        Error("malformed block record in AST file");
        return true;
      }
      continue;

    case BitstreamEntry::Record:
      Stream.skipRecord(Entry.ID);
      continue;
    }
  }
}

/// ParseMetadataAttachment - Parse metadata attachments.
bool BitcodeReader::ParseMetadataAttachment() {
  if (Stream.EnterSubBlock(bitc::METADATA_ATTACHMENT_ID))
    return Error("Malformed block record");

  SmallVector<uint64_t, 64> Record;
  while (1) {
    BitstreamEntry Entry = Stream.advanceSkippingSubblocks();

    switch (Entry.Kind) {
    case BitstreamEntry::SubBlock: // Handled for us already.
    case BitstreamEntry::Error:
      return Error("malformed metadata block");
    case BitstreamEntry::EndBlock:
      return false;
    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a metadata attachment record.
    Record.clear();
    switch (Stream.readRecord(Entry.ID, Record)) {
    default:  // Default behavior: ignore.
      break;
    case bitc::METADATA_ATTACHMENT: {
      unsigned RecordLength = Record.size();
      if (Record.empty() || (RecordLength - 1) % 2 == 1)
        return Error ("Invalid METADATA_ATTACHMENT reader!");
      Instruction *Inst = InstructionList[Record[0]];
      for (unsigned i = 1; i != RecordLength; i = i+2) {
        unsigned Kind = Record[i];
        DenseMap<unsigned, unsigned>::iterator I =
          MDKindMap.find(Kind);
        if (I == MDKindMap.end())
          return Error("Invalid metadata kind ID");
        Value *Node = MDValueList.getValueFwdRef(Record[i+1]);
        Inst->setMetadata(I->second, cast<MDNode>(Node));
      }
      break;
    }
    }
  }
}

/// ParseFunctionBody - Lazily parse the specified function body block.
bool BitcodeReader::ParseFunctionBody(Function *F) {
  if (Stream.EnterSubBlock(bitc::FUNCTION_BLOCK_ID))
    return Error("Malformed block record");

  InstructionList.clear();
  unsigned ModuleValueListSize = ValueList.size();
  unsigned ModuleMDValueListSize = MDValueList.size();

  // Add all the function arguments to the value table.
  for(Function::arg_iterator I = F->arg_begin(), E = F->arg_end(); I != E; ++I)
    ValueList.push_back(I);

  unsigned NextValueNo = ValueList.size();
  BasicBlock *CurBB = 0;
  unsigned CurBBNo = 0;

  DebugLoc LastLoc;

  // Read all the records.
  SmallVector<uint64_t, 64> Record;
  while (1) {
    BitstreamEntry Entry = Stream.advance();

    switch (Entry.Kind) {
    case BitstreamEntry::Error:
      return Error("Bitcode error in function block");
    case BitstreamEntry::EndBlock:
      goto OutOfRecordLoop;

    case BitstreamEntry::SubBlock:
      switch (Entry.ID) {
      default:  // Skip unknown content.
        if (Stream.SkipBlock())
          return Error("Malformed block record");
        break;
      case bitc::CONSTANTS_BLOCK_ID:
        if (ParseConstants()) return true;
        NextValueNo = ValueList.size();
        break;
      case bitc::VALUE_SYMTAB_BLOCK_ID:
        if (ParseValueSymbolTable()) return true;
        break;
      case bitc::METADATA_ATTACHMENT_ID:
        if (ParseMetadataAttachment()) return true;
        break;
      case bitc::METADATA_BLOCK_ID:
        if (ParseMetadata()) return true;
        break;
      }
      continue;

    case BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read a record.
    Record.clear();
    Instruction *I = 0;
    unsigned BitCode = Stream.readRecord(Entry.ID, Record);
    switch (BitCode) {
    default: // Default behavior: reject
      return Error("Unknown instruction");
    case bitc::FUNC_CODE_DECLAREBLOCKS:     // DECLAREBLOCKS: [nblocks]
      if (Record.size() < 1 || Record[0] == 0)
        return Error("Invalid DECLAREBLOCKS record");
      // Create all the basic blocks for the function.
      FunctionBBs.resize(Record[0]);
      for (unsigned i = 0, e = FunctionBBs.size(); i != e; ++i)
        FunctionBBs[i] = BasicBlock::Create(Context, "", F);
      CurBB = FunctionBBs[0];
      continue;

    case bitc::FUNC_CODE_DEBUG_LOC_AGAIN:  // DEBUG_LOC_AGAIN
      // This record indicates that the last instruction is at the same
      // location as the previous instruction with a location.
      I = 0;

      // Get the last instruction emitted.
      if (CurBB && !CurBB->empty())
        I = &CurBB->back();
      else if (CurBBNo && FunctionBBs[CurBBNo-1] &&
               !FunctionBBs[CurBBNo-1]->empty())
        I = &FunctionBBs[CurBBNo-1]->back();

      if (I == 0) return Error("Invalid DEBUG_LOC_AGAIN record");
      I->setDebugLoc(LastLoc);
      I = 0;
      continue;

    case bitc::FUNC_CODE_DEBUG_LOC: {      // DEBUG_LOC: [line, col, scope, ia]
      I = 0;     // Get the last instruction emitted.
      if (CurBB && !CurBB->empty())
        I = &CurBB->back();
      else if (CurBBNo && FunctionBBs[CurBBNo-1] &&
               !FunctionBBs[CurBBNo-1]->empty())
        I = &FunctionBBs[CurBBNo-1]->back();
      if (I == 0 || Record.size() < 4)
        return Error("Invalid FUNC_CODE_DEBUG_LOC record");

      unsigned Line = Record[0], Col = Record[1];
      unsigned ScopeID = Record[2], IAID = Record[3];

      MDNode *Scope = 0, *IA = 0;
      if (ScopeID) Scope = cast<MDNode>(MDValueList.getValueFwdRef(ScopeID-1));
      if (IAID)    IA = cast<MDNode>(MDValueList.getValueFwdRef(IAID-1));
      LastLoc = DebugLoc::get(Line, Col, Scope, IA);
      I->setDebugLoc(LastLoc);
      I = 0;
      continue;
    }

    case bitc::FUNC_CODE_INST_BINOP: {    // BINOP: [opval, ty, opval, opcode]
      unsigned OpNum = 0;
      Value *LHS, *RHS;
      if (getValueTypePair(Record, OpNum, NextValueNo, LHS) ||
          popValue(Record, OpNum, NextValueNo, LHS->getType(), RHS) ||
          OpNum+1 > Record.size())
        return Error("Invalid BINOP record");

      int Opc = GetDecodedBinaryOpcode(Record[OpNum++], LHS->getType());
      if (Opc == -1) return Error("Invalid BINOP record");
      I = BinaryOperator::Create((Instruction::BinaryOps)Opc, LHS, RHS);
      InstructionList.push_back(I);
      if (OpNum < Record.size()) {
        if (Opc == Instruction::Add ||
            Opc == Instruction::Sub ||
            Opc == Instruction::Mul ||
            Opc == Instruction::Shl) {
          if (Record[OpNum] & (1 << bitc::OBO_NO_SIGNED_WRAP))
            cast<BinaryOperator>(I)->setHasNoSignedWrap(true);
          if (Record[OpNum] & (1 << bitc::OBO_NO_UNSIGNED_WRAP))
            cast<BinaryOperator>(I)->setHasNoUnsignedWrap(true);
        } else if (Opc == Instruction::SDiv ||
                   Opc == Instruction::UDiv ||
                   Opc == Instruction::LShr ||
                   Opc == Instruction::AShr) {
          if (Record[OpNum] & (1 << bitc::PEO_EXACT))
            cast<BinaryOperator>(I)->setIsExact(true);
        } else if (isa<FPMathOperator>(I)) {
          FastMathFlags FMF;
          if (0 != (Record[OpNum] & FastMathFlags::UnsafeAlgebra))
            FMF.setUnsafeAlgebra();
          if (0 != (Record[OpNum] & FastMathFlags::NoNaNs))
            FMF.setNoNaNs();
          if (0 != (Record[OpNum] & FastMathFlags::NoInfs))
            FMF.setNoInfs();
          if (0 != (Record[OpNum] & FastMathFlags::NoSignedZeros))
            FMF.setNoSignedZeros();
          if (0 != (Record[OpNum] & FastMathFlags::AllowReciprocal))
            FMF.setAllowReciprocal();
          if (FMF.any())
            I->setFastMathFlags(FMF);
        }

      }
      break;
    }
    case bitc::FUNC_CODE_INST_CAST: {    // CAST: [opval, opty, destty, castopc]
      unsigned OpNum = 0;
      Value *Op;
      if (getValueTypePair(Record, OpNum, NextValueNo, Op) ||
          OpNum+2 != Record.size())
        return Error("Invalid CAST record");

      Type *ResTy = getTypeByID(Record[OpNum]);
      int Opc = GetDecodedCastOpcode(Record[OpNum+1]);
      if (Opc == -1 || ResTy == 0)
        return Error("Invalid CAST record");
      I = CastInst::Create((Instruction::CastOps)Opc, Op, ResTy);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_INBOUNDS_GEP:
    case bitc::FUNC_CODE_INST_GEP: { // GEP: [n x operands]
      unsigned OpNum = 0;
      Value *BasePtr;
      if (getValueTypePair(Record, OpNum, NextValueNo, BasePtr))
        return Error("Invalid GEP record");

      SmallVector<Value*, 16> GEPIdx;
      while (OpNum != Record.size()) {
        Value *Op;
        if (getValueTypePair(Record, OpNum, NextValueNo, Op))
          return Error("Invalid GEP record");
        GEPIdx.push_back(Op);
      }

      I = GetElementPtrInst::Create(BasePtr, GEPIdx);
      InstructionList.push_back(I);
      if (BitCode == bitc::FUNC_CODE_INST_INBOUNDS_GEP)
        cast<GetElementPtrInst>(I)->setIsInBounds(true);
      break;
    }

    case bitc::FUNC_CODE_INST_EXTRACTVAL: {
                                       // EXTRACTVAL: [opty, opval, n x indices]
      unsigned OpNum = 0;
      Value *Agg;
      if (getValueTypePair(Record, OpNum, NextValueNo, Agg))
        return Error("Invalid EXTRACTVAL record");

      SmallVector<unsigned, 4> EXTRACTVALIdx;
      for (unsigned RecSize = Record.size();
           OpNum != RecSize; ++OpNum) {
        uint64_t Index = Record[OpNum];
        if ((unsigned)Index != Index)
          return Error("Invalid EXTRACTVAL index");
        EXTRACTVALIdx.push_back((unsigned)Index);
      }

      I = ExtractValueInst::Create(Agg, EXTRACTVALIdx);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_INSERTVAL: {
                           // INSERTVAL: [opty, opval, opty, opval, n x indices]
      unsigned OpNum = 0;
      Value *Agg;
      if (getValueTypePair(Record, OpNum, NextValueNo, Agg))
        return Error("Invalid INSERTVAL record");
      Value *Val;
      if (getValueTypePair(Record, OpNum, NextValueNo, Val))
        return Error("Invalid INSERTVAL record");

      SmallVector<unsigned, 4> INSERTVALIdx;
      for (unsigned RecSize = Record.size();
           OpNum != RecSize; ++OpNum) {
        uint64_t Index = Record[OpNum];
        if ((unsigned)Index != Index)
          return Error("Invalid INSERTVAL index");
        INSERTVALIdx.push_back((unsigned)Index);
      }

      I = InsertValueInst::Create(Agg, Val, INSERTVALIdx);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_SELECT: { // SELECT: [opval, ty, opval, opval]
      // obsolete form of select
      // handles select i1 ... in old bitcode
      unsigned OpNum = 0;
      Value *TrueVal, *FalseVal, *Cond;
      if (getValueTypePair(Record, OpNum, NextValueNo, TrueVal) ||
          popValue(Record, OpNum, NextValueNo, TrueVal->getType(), FalseVal) ||
          popValue(Record, OpNum, NextValueNo, Type::getInt1Ty(Context), Cond))
        return Error("Invalid SELECT record");

      I = SelectInst::Create(Cond, TrueVal, FalseVal);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_VSELECT: {// VSELECT: [ty,opval,opval,predty,pred]
      // new form of select
      // handles select i1 or select [N x i1]
      unsigned OpNum = 0;
      Value *TrueVal, *FalseVal, *Cond;
      if (getValueTypePair(Record, OpNum, NextValueNo, TrueVal) ||
          popValue(Record, OpNum, NextValueNo, TrueVal->getType(), FalseVal) ||
          getValueTypePair(Record, OpNum, NextValueNo, Cond))
        return Error("Invalid SELECT record");

      // select condition can be either i1 or [N x i1]
      if (VectorType* vector_type =
          dyn_cast<VectorType>(Cond->getType())) {
        // expect <n x i1>
        if (vector_type->getElementType() != Type::getInt1Ty(Context))
          return Error("Invalid SELECT condition type");
      } else {
        // expect i1
        if (Cond->getType() != Type::getInt1Ty(Context))
          return Error("Invalid SELECT condition type");
      }

      I = SelectInst::Create(Cond, TrueVal, FalseVal);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_EXTRACTELT: { // EXTRACTELT: [opty, opval, opval]
      unsigned OpNum = 0;
      Value *Vec, *Idx;
      if (getValueTypePair(Record, OpNum, NextValueNo, Vec) ||
          popValue(Record, OpNum, NextValueNo, Type::getInt32Ty(Context), Idx))
        return Error("Invalid EXTRACTELT record");
      I = ExtractElementInst::Create(Vec, Idx);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_INSERTELT: { // INSERTELT: [ty, opval,opval,opval]
      unsigned OpNum = 0;
      Value *Vec, *Elt, *Idx;
      if (getValueTypePair(Record, OpNum, NextValueNo, Vec) ||
          popValue(Record, OpNum, NextValueNo,
                   cast<VectorType>(Vec->getType())->getElementType(), Elt) ||
          popValue(Record, OpNum, NextValueNo, Type::getInt32Ty(Context), Idx))
        return Error("Invalid INSERTELT record");
      I = InsertElementInst::Create(Vec, Elt, Idx);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_SHUFFLEVEC: {// SHUFFLEVEC: [opval,ty,opval,opval]
      unsigned OpNum = 0;
      Value *Vec1, *Vec2, *Mask;
      if (getValueTypePair(Record, OpNum, NextValueNo, Vec1) ||
          popValue(Record, OpNum, NextValueNo, Vec1->getType(), Vec2))
        return Error("Invalid SHUFFLEVEC record");

      if (getValueTypePair(Record, OpNum, NextValueNo, Mask))
        return Error("Invalid SHUFFLEVEC record");
      I = new ShuffleVectorInst(Vec1, Vec2, Mask);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_CMP:   // CMP: [opty, opval, opval, pred]
      // Old form of ICmp/FCmp returning bool
      // Existed to differentiate between icmp/fcmp and vicmp/vfcmp which were
      // both legal on vectors but had different behaviour.
    case bitc::FUNC_CODE_INST_CMP2: { // CMP2: [opty, opval, opval, pred]
      // FCmp/ICmp returning bool or vector of bool

      unsigned OpNum = 0;
      Value *LHS, *RHS;
      if (getValueTypePair(Record, OpNum, NextValueNo, LHS) ||
          popValue(Record, OpNum, NextValueNo, LHS->getType(), RHS) ||
          OpNum+1 != Record.size())
        return Error("Invalid CMP record");

      if (LHS->getType()->isFPOrFPVectorTy())
        I = new FCmpInst((FCmpInst::Predicate)Record[OpNum], LHS, RHS);
      else
        I = new ICmpInst((ICmpInst::Predicate)Record[OpNum], LHS, RHS);
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_RET: // RET: [opty,opval<optional>]
      {
        unsigned Size = Record.size();
        if (Size == 0) {
          I = ReturnInst::Create(Context);
          InstructionList.push_back(I);
          break;
        }

        unsigned OpNum = 0;
        Value *Op = NULL;
        if (getValueTypePair(Record, OpNum, NextValueNo, Op))
          return Error("Invalid RET record");
        if (OpNum != Record.size())
          return Error("Invalid RET record");

        I = ReturnInst::Create(Context, Op);
        InstructionList.push_back(I);
        break;
      }
    case bitc::FUNC_CODE_INST_BR: { // BR: [bb#, bb#, opval] or [bb#]
      if (Record.size() != 1 && Record.size() != 3)
        return Error("Invalid BR record");
      BasicBlock *TrueDest = getBasicBlock(Record[0]);
      if (TrueDest == 0)
        return Error("Invalid BR record");

      if (Record.size() == 1) {
        I = BranchInst::Create(TrueDest);
        InstructionList.push_back(I);
      }
      else {
        BasicBlock *FalseDest = getBasicBlock(Record[1]);
        Value *Cond = getValue(Record, 2, NextValueNo,
                               Type::getInt1Ty(Context));
        if (FalseDest == 0 || Cond == 0)
          return Error("Invalid BR record");
        I = BranchInst::Create(TrueDest, FalseDest, Cond);
        InstructionList.push_back(I);
      }
      break;
    }
    case bitc::FUNC_CODE_INST_SWITCH: { // SWITCH: [opty, op0, op1, ...]
      // Check magic
      if ((Record[0] >> 16) == SWITCH_INST_MAGIC) {
        // "New" SwitchInst format with case ranges. The changes to write this
        // format were reverted but we still recognize bitcode that uses it.
        // Hopefully someday we will have support for case ranges and can use
        // this format again.

        Type *OpTy = getTypeByID(Record[1]);
        unsigned ValueBitWidth = cast<IntegerType>(OpTy)->getBitWidth();

        Value *Cond = getValue(Record, 2, NextValueNo, OpTy);
        BasicBlock *Default = getBasicBlock(Record[3]);
        if (OpTy == 0 || Cond == 0 || Default == 0)
          return Error("Invalid SWITCH record");

        unsigned NumCases = Record[4];

        SwitchInst *SI = SwitchInst::Create(Cond, Default, NumCases);
        InstructionList.push_back(SI);

        unsigned CurIdx = 5;
        for (unsigned i = 0; i != NumCases; ++i) {
          SmallVector<ConstantInt*, 1> CaseVals;
          unsigned NumItems = Record[CurIdx++];
          for (unsigned ci = 0; ci != NumItems; ++ci) {
            bool isSingleNumber = Record[CurIdx++];

            APInt Low;
            unsigned ActiveWords = 1;
            if (ValueBitWidth > 64)
              ActiveWords = Record[CurIdx++];
            Low = ReadWideAPInt(makeArrayRef(&Record[CurIdx], ActiveWords),
                                ValueBitWidth);
            CurIdx += ActiveWords;

            if (!isSingleNumber) {
              ActiveWords = 1;
              if (ValueBitWidth > 64)
                ActiveWords = Record[CurIdx++];
              APInt High =
                  ReadWideAPInt(makeArrayRef(&Record[CurIdx], ActiveWords),
                                ValueBitWidth);
              CurIdx += ActiveWords;

              // FIXME: It is not clear whether values in the range should be
              // compared as signed or unsigned values. The partially
              // implemented changes that used this format in the past used
              // unsigned comparisons.
              for ( ; Low.ule(High); ++Low)
                CaseVals.push_back(ConstantInt::get(Context, Low));
            } else
              CaseVals.push_back(ConstantInt::get(Context, Low));
          }
          BasicBlock *DestBB = getBasicBlock(Record[CurIdx++]);
          for (SmallVector<ConstantInt*, 1>::iterator cvi = CaseVals.begin(),
                 cve = CaseVals.end(); cvi != cve; ++cvi)
            SI->addCase(*cvi, DestBB);
        }
        I = SI;
        break;
      }

      // Old SwitchInst format without case ranges.

      if (Record.size() < 3 || (Record.size() & 1) == 0)
        return Error("Invalid SWITCH record");
      Type *OpTy = getTypeByID(Record[0]);
      Value *Cond = getValue(Record, 1, NextValueNo, OpTy);
      BasicBlock *Default = getBasicBlock(Record[2]);
      if (OpTy == 0 || Cond == 0 || Default == 0)
        return Error("Invalid SWITCH record");
      unsigned NumCases = (Record.size()-3)/2;
      SwitchInst *SI = SwitchInst::Create(Cond, Default, NumCases);
      InstructionList.push_back(SI);
      for (unsigned i = 0, e = NumCases; i != e; ++i) {
        ConstantInt *CaseVal =
          dyn_cast_or_null<ConstantInt>(getFnValueByID(Record[3+i*2], OpTy));
        BasicBlock *DestBB = getBasicBlock(Record[1+3+i*2]);
        if (CaseVal == 0 || DestBB == 0) {
          delete SI;
          return Error("Invalid SWITCH record!");
        }
        SI->addCase(CaseVal, DestBB);
      }
      I = SI;
      break;
    }
    case bitc::FUNC_CODE_INST_INDIRECTBR: { // INDIRECTBR: [opty, op0, op1, ...]
      if (Record.size() < 2)
        return Error("Invalid INDIRECTBR record");
      Type *OpTy = getTypeByID(Record[0]);
      Value *Address = getValue(Record, 1, NextValueNo, OpTy);
      if (OpTy == 0 || Address == 0)
        return Error("Invalid INDIRECTBR record");
      unsigned NumDests = Record.size()-2;
      IndirectBrInst *IBI = IndirectBrInst::Create(Address, NumDests);
      InstructionList.push_back(IBI);
      for (unsigned i = 0, e = NumDests; i != e; ++i) {
        if (BasicBlock *DestBB = getBasicBlock(Record[2+i])) {
          IBI->addDestination(DestBB);
        } else {
          delete IBI;
          return Error("Invalid INDIRECTBR record!");
        }
      }
      I = IBI;
      break;
    }

    case bitc::FUNC_CODE_INST_INVOKE: {
      // INVOKE: [attrs, cc, normBB, unwindBB, fnty, op0,op1,op2, ...]
      if (Record.size() < 4) return Error("Invalid INVOKE record");
      AttributeSet PAL = getAttributes(Record[0]);
      unsigned CCInfo = Record[1];
      BasicBlock *NormalBB = getBasicBlock(Record[2]);
      BasicBlock *UnwindBB = getBasicBlock(Record[3]);

      unsigned OpNum = 4;
      Value *Callee;
      if (getValueTypePair(Record, OpNum, NextValueNo, Callee))
        return Error("Invalid INVOKE record");

      PointerType *CalleeTy = dyn_cast<PointerType>(Callee->getType());
      FunctionType *FTy = !CalleeTy ? 0 :
        dyn_cast<FunctionType>(CalleeTy->getElementType());

      // Check that the right number of fixed parameters are here.
      if (FTy == 0 || NormalBB == 0 || UnwindBB == 0 ||
          Record.size() < OpNum+FTy->getNumParams())
        return Error("Invalid INVOKE record");

      SmallVector<Value*, 16> Ops;
      for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i, ++OpNum) {
        Ops.push_back(getValue(Record, OpNum, NextValueNo,
                               FTy->getParamType(i)));
        if (Ops.back() == 0) return Error("Invalid INVOKE record");
      }

      if (!FTy->isVarArg()) {
        if (Record.size() != OpNum)
          return Error("Invalid INVOKE record");
      } else {
        // Read type/value pairs for varargs params.
        while (OpNum != Record.size()) {
          Value *Op;
          if (getValueTypePair(Record, OpNum, NextValueNo, Op))
            return Error("Invalid INVOKE record");
          Ops.push_back(Op);
        }
      }

      I = InvokeInst::Create(Callee, NormalBB, UnwindBB, Ops);
      InstructionList.push_back(I);
      cast<InvokeInst>(I)->setCallingConv(
        static_cast<CallingConv::ID>(CCInfo));
      cast<InvokeInst>(I)->setAttributes(PAL);
      break;
    }
    case bitc::FUNC_CODE_INST_RESUME: { // RESUME: [opval]
      unsigned Idx = 0;
      Value *Val = 0;
      if (getValueTypePair(Record, Idx, NextValueNo, Val))
        return Error("Invalid RESUME record");
      I = ResumeInst::Create(Val);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_UNREACHABLE: // UNREACHABLE
      I = new UnreachableInst(Context);
      InstructionList.push_back(I);
      break;
    case bitc::FUNC_CODE_INST_PHI: { // PHI: [ty, val0,bb0, ...]
      if (Record.size() < 1 || ((Record.size()-1)&1))
        return Error("Invalid PHI record");
      Type *Ty = getTypeByID(Record[0]);
      if (!Ty) return Error("Invalid PHI record");

      PHINode *PN = PHINode::Create(Ty, (Record.size()-1)/2);
      InstructionList.push_back(PN);

      for (unsigned i = 0, e = Record.size()-1; i != e; i += 2) {
        Value *V;
        // With the new function encoding, it is possible that operands have
        // negative IDs (for forward references).  Use a signed VBR
        // representation to keep the encoding small.
        if (UseRelativeIDs)
          V = getValueSigned(Record, 1+i, NextValueNo, Ty);
        else
          V = getValue(Record, 1+i, NextValueNo, Ty);
        BasicBlock *BB = getBasicBlock(Record[2+i]);
        if (!V || !BB) return Error("Invalid PHI record");
        PN->addIncoming(V, BB);
      }
      I = PN;
      break;
    }

    case bitc::FUNC_CODE_INST_LANDINGPAD: {
      // LANDINGPAD: [ty, val, val, num, (id0,val0 ...)?]
      unsigned Idx = 0;
      if (Record.size() < 4)
        return Error("Invalid LANDINGPAD record");
      Type *Ty = getTypeByID(Record[Idx++]);
      if (!Ty) return Error("Invalid LANDINGPAD record");
      Value *PersFn = 0;
      if (getValueTypePair(Record, Idx, NextValueNo, PersFn))
        return Error("Invalid LANDINGPAD record");

      bool IsCleanup = !!Record[Idx++];
      unsigned NumClauses = Record[Idx++];
      LandingPadInst *LP = LandingPadInst::Create(Ty, PersFn, NumClauses);
      LP->setCleanup(IsCleanup);
      for (unsigned J = 0; J != NumClauses; ++J) {
        LandingPadInst::ClauseType CT =
          LandingPadInst::ClauseType(Record[Idx++]); (void)CT;
        Value *Val;

        if (getValueTypePair(Record, Idx, NextValueNo, Val)) {
          delete LP;
          return Error("Invalid LANDINGPAD record");
        }

        assert((CT != LandingPadInst::Catch ||
                !isa<ArrayType>(Val->getType())) &&
               "Catch clause has a invalid type!");
        assert((CT != LandingPadInst::Filter ||
                isa<ArrayType>(Val->getType())) &&
               "Filter clause has invalid type!");
        LP->addClause(Val);
      }

      I = LP;
      InstructionList.push_back(I);
      break;
    }

    case bitc::FUNC_CODE_INST_ALLOCA: { // ALLOCA: [instty, opty, op, align]
      if (Record.size() != 4)
        return Error("Invalid ALLOCA record");
      PointerType *Ty =
        dyn_cast_or_null<PointerType>(getTypeByID(Record[0]));
      Type *OpTy = getTypeByID(Record[1]);
      Value *Size = getFnValueByID(Record[2], OpTy);
      unsigned Align = Record[3];
      if (!Ty || !Size) return Error("Invalid ALLOCA record");
      I = new AllocaInst(Ty->getElementType(), Size, (1 << Align) >> 1);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_LOAD: { // LOAD: [opty, op, align, vol]
      unsigned OpNum = 0;
      Value *Op;
      if (getValueTypePair(Record, OpNum, NextValueNo, Op) ||
          OpNum+2 != Record.size())
        return Error("Invalid LOAD record");

      I = new LoadInst(Op, "", Record[OpNum+1], (1 << Record[OpNum]) >> 1);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_LOADATOMIC: {
       // LOADATOMIC: [opty, op, align, vol, ordering, synchscope]
      unsigned OpNum = 0;
      Value *Op;
      if (getValueTypePair(Record, OpNum, NextValueNo, Op) ||
          OpNum+4 != Record.size())
        return Error("Invalid LOADATOMIC record");


      AtomicOrdering Ordering = GetDecodedOrdering(Record[OpNum+2]);
      if (Ordering == NotAtomic || Ordering == Release ||
          Ordering == AcquireRelease)
        return Error("Invalid LOADATOMIC record");
      if (Ordering != NotAtomic && Record[OpNum] == 0)
        return Error("Invalid LOADATOMIC record");
      SynchronizationScope SynchScope = GetDecodedSynchScope(Record[OpNum+3]);

      I = new LoadInst(Op, "", Record[OpNum+1], (1 << Record[OpNum]) >> 1,
                       Ordering, SynchScope);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_STORE: { // STORE2:[ptrty, ptr, val, align, vol]
      unsigned OpNum = 0;
      Value *Val, *Ptr;
      if (getValueTypePair(Record, OpNum, NextValueNo, Ptr) ||
          popValue(Record, OpNum, NextValueNo,
                    cast<PointerType>(Ptr->getType())->getElementType(), Val) ||
          OpNum+2 != Record.size())
        return Error("Invalid STORE record");

      I = new StoreInst(Val, Ptr, Record[OpNum+1], (1 << Record[OpNum]) >> 1);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_STOREATOMIC: {
      // STOREATOMIC: [ptrty, ptr, val, align, vol, ordering, synchscope]
      unsigned OpNum = 0;
      Value *Val, *Ptr;
      if (getValueTypePair(Record, OpNum, NextValueNo, Ptr) ||
          popValue(Record, OpNum, NextValueNo,
                    cast<PointerType>(Ptr->getType())->getElementType(), Val) ||
          OpNum+4 != Record.size())
        return Error("Invalid STOREATOMIC record");

      AtomicOrdering Ordering = GetDecodedOrdering(Record[OpNum+2]);
      if (Ordering == NotAtomic || Ordering == Acquire ||
          Ordering == AcquireRelease)
        return Error("Invalid STOREATOMIC record");
      SynchronizationScope SynchScope = GetDecodedSynchScope(Record[OpNum+3]);
      if (Ordering != NotAtomic && Record[OpNum] == 0)
        return Error("Invalid STOREATOMIC record");

      I = new StoreInst(Val, Ptr, Record[OpNum+1], (1 << Record[OpNum]) >> 1,
                        Ordering, SynchScope);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_CMPXCHG: {
      // CMPXCHG:[ptrty, ptr, cmp, new, vol, ordering, synchscope]
      unsigned OpNum = 0;
      Value *Ptr, *Cmp, *New;
      if (getValueTypePair(Record, OpNum, NextValueNo, Ptr) ||
          popValue(Record, OpNum, NextValueNo,
                    cast<PointerType>(Ptr->getType())->getElementType(), Cmp) ||
          popValue(Record, OpNum, NextValueNo,
                    cast<PointerType>(Ptr->getType())->getElementType(), New) ||
          OpNum+3 != Record.size())
        return Error("Invalid CMPXCHG record");
      AtomicOrdering Ordering = GetDecodedOrdering(Record[OpNum+1]);
      if (Ordering == NotAtomic || Ordering == Unordered)
        return Error("Invalid CMPXCHG record");
      SynchronizationScope SynchScope = GetDecodedSynchScope(Record[OpNum+2]);
      I = new AtomicCmpXchgInst(Ptr, Cmp, New, Ordering, SynchScope);
      cast<AtomicCmpXchgInst>(I)->setVolatile(Record[OpNum]);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_ATOMICRMW: {
      // ATOMICRMW:[ptrty, ptr, val, op, vol, ordering, synchscope]
      unsigned OpNum = 0;
      Value *Ptr, *Val;
      if (getValueTypePair(Record, OpNum, NextValueNo, Ptr) ||
          popValue(Record, OpNum, NextValueNo,
                    cast<PointerType>(Ptr->getType())->getElementType(), Val) ||
          OpNum+4 != Record.size())
        return Error("Invalid ATOMICRMW record");
      AtomicRMWInst::BinOp Operation = GetDecodedRMWOperation(Record[OpNum]);
      if (Operation < AtomicRMWInst::FIRST_BINOP ||
          Operation > AtomicRMWInst::LAST_BINOP)
        return Error("Invalid ATOMICRMW record");
      AtomicOrdering Ordering = GetDecodedOrdering(Record[OpNum+2]);
      if (Ordering == NotAtomic || Ordering == Unordered)
        return Error("Invalid ATOMICRMW record");
      SynchronizationScope SynchScope = GetDecodedSynchScope(Record[OpNum+3]);
      I = new AtomicRMWInst(Operation, Ptr, Val, Ordering, SynchScope);
      cast<AtomicRMWInst>(I)->setVolatile(Record[OpNum+1]);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_FENCE: { // FENCE:[ordering, synchscope]
      if (2 != Record.size())
        return Error("Invalid FENCE record");
      AtomicOrdering Ordering = GetDecodedOrdering(Record[0]);
      if (Ordering == NotAtomic || Ordering == Unordered ||
          Ordering == Monotonic)
        return Error("Invalid FENCE record");
      SynchronizationScope SynchScope = GetDecodedSynchScope(Record[1]);
      I = new FenceInst(Context, Ordering, SynchScope);
      InstructionList.push_back(I);
      break;
    }
    case bitc::FUNC_CODE_INST_CALL: {
      // CALL: [paramattrs, cc, fnty, fnid, arg0, arg1...]
      if (Record.size() < 3)
        return Error("Invalid CALL record");

      AttributeSet PAL = getAttributes(Record[0]);
      unsigned CCInfo = Record[1];

      unsigned OpNum = 2;
      Value *Callee;
      if (getValueTypePair(Record, OpNum, NextValueNo, Callee))
        return Error("Invalid CALL record");

      PointerType *OpTy = dyn_cast<PointerType>(Callee->getType());
      FunctionType *FTy = 0;
      if (OpTy) FTy = dyn_cast<FunctionType>(OpTy->getElementType());
      if (!FTy || Record.size() < FTy->getNumParams()+OpNum)
        return Error("Invalid CALL record");

      SmallVector<Value*, 16> Args;
      // Read the fixed params.
      for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i, ++OpNum) {
        if (FTy->getParamType(i)->isLabelTy())
          Args.push_back(getBasicBlock(Record[OpNum]));
        else
          Args.push_back(getValue(Record, OpNum, NextValueNo,
                                  FTy->getParamType(i)));
        if (Args.back() == 0) return Error("Invalid CALL record");
      }

      // Read type/value pairs for varargs params.
      if (!FTy->isVarArg()) {
        if (OpNum != Record.size())
          return Error("Invalid CALL record");
      } else {
        while (OpNum != Record.size()) {
          Value *Op;
          if (getValueTypePair(Record, OpNum, NextValueNo, Op))
            return Error("Invalid CALL record");
          Args.push_back(Op);
        }
      }

      I = CallInst::Create(Callee, Args);
      InstructionList.push_back(I);
      cast<CallInst>(I)->setCallingConv(
        static_cast<CallingConv::ID>(CCInfo>>1));
      cast<CallInst>(I)->setTailCall(CCInfo & 1);
      cast<CallInst>(I)->setAttributes(PAL);
      break;
    }
    case bitc::FUNC_CODE_INST_VAARG: { // VAARG: [valistty, valist, instty]
      if (Record.size() < 3)
        return Error("Invalid VAARG record");
      Type *OpTy = getTypeByID(Record[0]);
      Value *Op = getValue(Record, 1, NextValueNo, OpTy);
      Type *ResTy = getTypeByID(Record[2]);
      if (!OpTy || !Op || !ResTy)
        return Error("Invalid VAARG record");
      I = new VAArgInst(Op, ResTy);
      InstructionList.push_back(I);
      break;
    }
    }

    // Add instruction to end of current BB.  If there is no current BB, reject
    // this file.
    if (CurBB == 0) {
      delete I;
      return Error("Invalid instruction with no BB");
    }
    CurBB->getInstList().push_back(I);

    // If this was a terminator instruction, move to the next block.
    if (isa<TerminatorInst>(I)) {
      ++CurBBNo;
      CurBB = CurBBNo < FunctionBBs.size() ? FunctionBBs[CurBBNo] : 0;
    }

    // Non-void values get registered in the value table for future use.
    if (I && !I->getType()->isVoidTy())
      ValueList.AssignValue(I, NextValueNo++);
  }

OutOfRecordLoop:

  // Check the function list for unresolved values.
  if (Argument *A = dyn_cast<Argument>(ValueList.back())) {
    if (A->getParent() == 0) {
      // We found at least one unresolved value.  Nuke them all to avoid leaks.
      for (unsigned i = ModuleValueListSize, e = ValueList.size(); i != e; ++i){
        if ((A = dyn_cast<Argument>(ValueList[i])) && A->getParent() == 0) {
          A->replaceAllUsesWith(UndefValue::get(A->getType()));
          delete A;
        }
      }
      return Error("Never resolved value found in function!");
    }
  }

  // FIXME: Check for unresolved forward-declared metadata references
  // and clean up leaks.

  // See if anything took the address of blocks in this function.  If so,
  // resolve them now.
  DenseMap<Function*, std::vector<BlockAddrRefTy> >::iterator BAFRI =
    BlockAddrFwdRefs.find(F);
  if (BAFRI != BlockAddrFwdRefs.end()) {
    std::vector<BlockAddrRefTy> &RefList = BAFRI->second;
    for (unsigned i = 0, e = RefList.size(); i != e; ++i) {
      unsigned BlockIdx = RefList[i].first;
      if (BlockIdx >= FunctionBBs.size())
        return Error("Invalid blockaddress block #");

      GlobalVariable *FwdRef = RefList[i].second;
      FwdRef->replaceAllUsesWith(BlockAddress::get(F, FunctionBBs[BlockIdx]));
      FwdRef->eraseFromParent();
    }

    BlockAddrFwdRefs.erase(BAFRI);
  }

  // Trim the value list down to the size it was before we parsed this function.
  ValueList.shrinkTo(ModuleValueListSize);
  MDValueList.shrinkTo(ModuleMDValueListSize);
  std::vector<BasicBlock*>().swap(FunctionBBs);
  return false;
}

/// FindFunctionInStream - Find the function body in the bitcode stream
bool BitcodeReader::FindFunctionInStream(Function *F,
       DenseMap<Function*, uint64_t>::iterator DeferredFunctionInfoIterator) {
  while (DeferredFunctionInfoIterator->second == 0) {
    if (Stream.AtEndOfStream())
      return Error("Could not find Function in stream");
    // ParseModule will parse the next body in the stream and set its
    // position in the DeferredFunctionInfo map.
    if (ParseModule(true)) return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// GVMaterializer implementation
//===----------------------------------------------------------------------===//


bool BitcodeReader::isMaterializable(const GlobalValue *GV) const {
  if (const Function *F = dyn_cast<Function>(GV)) {
    return F->isDeclaration() &&
      DeferredFunctionInfo.count(const_cast<Function*>(F));
  }
  return false;
}

bool BitcodeReader::Materialize(GlobalValue *GV, std::string *ErrInfo) {
  Function *F = dyn_cast<Function>(GV);
  // If it's not a function or is already material, ignore the request.
  if (!F || !F->isMaterializable()) return false;

  DenseMap<Function*, uint64_t>::iterator DFII = DeferredFunctionInfo.find(F);
  assert(DFII != DeferredFunctionInfo.end() && "Deferred function not found!");
  // If its position is recorded as 0, its body is somewhere in the stream
  // but we haven't seen it yet.
  if (DFII->second == 0)
    if (LazyStreamer && FindFunctionInStream(F, DFII)) return true;

  // Move the bit stream to the saved position of the deferred function body.
  Stream.JumpToBit(DFII->second);

  if (ParseFunctionBody(F)) {
    if (ErrInfo) *ErrInfo = ErrorString;
    return true;
  }

  // Upgrade any old intrinsic calls in the function.
  for (UpgradedIntrinsicMap::iterator I = UpgradedIntrinsics.begin(),
       E = UpgradedIntrinsics.end(); I != E; ++I) {
    if (I->first != I->second) {
      for (Value::use_iterator UI = I->first->use_begin(),
           UE = I->first->use_end(); UI != UE; ) {
        if (CallInst* CI = dyn_cast<CallInst>(*UI++))
          UpgradeIntrinsicCall(CI, I->second);
      }
    }
  }

  return false;
}

bool BitcodeReader::isDematerializable(const GlobalValue *GV) const {
  const Function *F = dyn_cast<Function>(GV);
  if (!F || F->isDeclaration())
    return false;
  return DeferredFunctionInfo.count(const_cast<Function*>(F));
}

void BitcodeReader::Dematerialize(GlobalValue *GV) {
  Function *F = dyn_cast<Function>(GV);
  // If this function isn't dematerializable, this is a noop.
  if (!F || !isDematerializable(F))
    return;

  assert(DeferredFunctionInfo.count(F) && "No info to read function later?");

  // Just forget the function body, we can remat it later.
  F->deleteBody();
}


bool BitcodeReader::MaterializeModule(Module *M, std::string *ErrInfo) {
  assert(M == TheModule &&
         "Can only Materialize the Module this BitcodeReader is attached to.");
  // Iterate over the module, deserializing any functions that are still on
  // disk.
  for (Module::iterator F = TheModule->begin(), E = TheModule->end();
       F != E; ++F)
    if (F->isMaterializable() &&
        Materialize(F, ErrInfo))
      return true;

  // At this point, if there are any function bodies, the current bit is
  // pointing to the END_BLOCK record after them. Now make sure the rest
  // of the bits in the module have been read.
  if (NextUnreadBit)
    ParseModule(true);

  // Upgrade any intrinsic calls that slipped through (should not happen!) and
  // delete the old functions to clean up. We can't do this unless the entire
  // module is materialized because there could always be another function body
  // with calls to the old function.
  for (std::vector<std::pair<Function*, Function*> >::iterator I =
       UpgradedIntrinsics.begin(), E = UpgradedIntrinsics.end(); I != E; ++I) {
    if (I->first != I->second) {
      for (Value::use_iterator UI = I->first->use_begin(),
           UE = I->first->use_end(); UI != UE; ) {
        if (CallInst* CI = dyn_cast<CallInst>(*UI++))
          UpgradeIntrinsicCall(CI, I->second);
      }
      if (!I->first->use_empty())
        I->first->replaceAllUsesWith(I->second);
      I->first->eraseFromParent();
    }
  }
  std::vector<std::pair<Function*, Function*> >().swap(UpgradedIntrinsics);

  return false;
}

bool BitcodeReader::InitStream() {
  if (LazyStreamer) return InitLazyStream();
  return InitStreamFromBuffer();
}

bool BitcodeReader::InitStreamFromBuffer() {
  const unsigned char *BufPtr = (const unsigned char*)Buffer->getBufferStart();
  const unsigned char *BufEnd = BufPtr+Buffer->getBufferSize();

  if (Buffer->getBufferSize() & 3) {
    if (!isRawBitcode(BufPtr, BufEnd) && !isBitcodeWrapper(BufPtr, BufEnd))
      return Error("Invalid bitcode signature");
    else
      return Error("Bitcode stream should be a multiple of 4 bytes in length");
  }

  // If we have a wrapper header, parse it and ignore the non-bc file contents.
  // The magic number is 0x0B17C0DE stored in little endian.
  if (isBitcodeWrapper(BufPtr, BufEnd))
    if (SkipBitcodeWrapperHeader(BufPtr, BufEnd, true))
      return Error("Invalid bitcode wrapper header");

  StreamFile.reset(new BitstreamReader(BufPtr, BufEnd));
  Stream.init(*StreamFile);

  return false;
}

bool BitcodeReader::InitLazyStream() {
  // Check and strip off the bitcode wrapper; BitstreamReader expects never to
  // see it.
  StreamingMemoryObject *Bytes = new StreamingMemoryObject(LazyStreamer);
  StreamFile.reset(new BitstreamReader(Bytes));
  Stream.init(*StreamFile);

  unsigned char buf[16];
  if (Bytes->readBytes(0, 16, buf) == -1)
    return Error("Bitcode stream must be at least 16 bytes in length");

  if (!isBitcode(buf, buf + 16))
    return Error("Invalid bitcode signature");

  if (isBitcodeWrapper(buf, buf + 4)) {
    const unsigned char *bitcodeStart = buf;
    const unsigned char *bitcodeEnd = buf + 16;
    SkipBitcodeWrapperHeader(bitcodeStart, bitcodeEnd, false);
    Bytes->dropLeadingBytes(bitcodeStart - buf);
    Bytes->setKnownObjectSize(bitcodeEnd - bitcodeStart);
  }
  return false;
}

//===----------------------------------------------------------------------===//
// External interface
//===----------------------------------------------------------------------===//

/// getLazyBitcodeModule - lazy function-at-a-time loading from a file.
///
Module *llvm::getLazyBitcodeModule(MemoryBuffer *Buffer,
                                   LLVMContext& Context,
                                   std::string *ErrMsg) {
  Module *M = new Module(Buffer->getBufferIdentifier(), Context);
  BitcodeReader *R = new BitcodeReader(Buffer, Context);
  M->setMaterializer(R);
  if (R->ParseBitcodeInto(M)) {
    if (ErrMsg)
      *ErrMsg = R->getErrorString();

    delete M;  // Also deletes R.
    return 0;
  }
  // Have the BitcodeReader dtor delete 'Buffer'.
  R->setBufferOwned(true);

  R->materializeForwardReferencedFunctions();

  return M;
}


Module *llvm::getStreamedBitcodeModule(const std::string &name,
                                       DataStreamer *streamer,
                                       LLVMContext &Context,
                                       std::string *ErrMsg) {
  Module *M = new Module(name, Context);
  BitcodeReader *R = new BitcodeReader(streamer, Context);
  M->setMaterializer(R);
  if (R->ParseBitcodeInto(M)) {
    if (ErrMsg)
      *ErrMsg = R->getErrorString();
    delete M;  // Also deletes R.
    return 0;
  }
  R->setBufferOwned(false); // no buffer to delete
  return M;
}

/// ParseBitcodeFile - Read the specified bitcode file, returning the module.
/// If an error occurs, return null and fill in *ErrMsg if non-null.
Module *llvm::ParseBitcodeFile(MemoryBuffer *Buffer, LLVMContext& Context,
                               std::string *ErrMsg){
  Module *M = getLazyBitcodeModule(Buffer, Context, ErrMsg);
  if (!M) return 0;

  // Don't let the BitcodeReader dtor delete 'Buffer', regardless of whether
  // there was an error.
  static_cast<BitcodeReader*>(M->getMaterializer())->setBufferOwned(false);

  // Read in the entire module, and destroy the BitcodeReader.
  if (M->MaterializeAllPermanently(ErrMsg)) {
    delete M;
    return 0;
  }

  // TODO: Restore the use-lists to the in-memory state when the bitcode was
  // written.  We must defer until the Module has been fully materialized.

  return M;
}

std::string llvm::getBitcodeTargetTriple(MemoryBuffer *Buffer,
                                         LLVMContext& Context,
                                         std::string *ErrMsg) {
  BitcodeReader *R = new BitcodeReader(Buffer, Context);
  // Don't let the BitcodeReader dtor delete 'Buffer'.
  R->setBufferOwned(false);

  std::string Triple("");
  if (R->ParseTriple(Triple))
    if (ErrMsg)
      *ErrMsg = R->getErrorString();

  delete R;
  return Triple;
}
