//===-- SystemZISelLowering.cpp - SystemZ DAG lowering implementation -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the SystemZTargetLowering class.
//
//===----------------------------------------------------------------------===//

#include "SystemZISelLowering.h"
#include "SystemZCallingConv.h"
#include "SystemZConstantPoolValue.h"
#include "SystemZMachineFunctionInfo.h"
#include "SystemZTargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/Intrinsics.h"
#include <cctype>

using namespace llvm;

#define DEBUG_TYPE "systemz-lower"

namespace {
// Represents a sequence for extracting a 0/1 value from an IPM result:
// (((X ^ XORValue) + AddValue) >> Bit)
struct IPMConversion {
  IPMConversion(unsigned xorValue, int64_t addValue, unsigned bit)
    : XORValue(xorValue), AddValue(addValue), Bit(bit) {}

  int64_t XORValue;
  int64_t AddValue;
  unsigned Bit;
};

// Represents information about a comparison.
struct Comparison {
  Comparison(SDValue Op0In, SDValue Op1In)
    : Op0(Op0In), Op1(Op1In), Opcode(0), ICmpType(0), CCValid(0), CCMask(0) {}

  // The operands to the comparison.
  SDValue Op0, Op1;

  // The opcode that should be used to compare Op0 and Op1.
  unsigned Opcode;

  // A SystemZICMP value.  Only used for integer comparisons.
  unsigned ICmpType;

  // The mask of CC values that Opcode can produce.
  unsigned CCValid;

  // The mask of CC values for which the original condition is true.
  unsigned CCMask;
};
} // end anonymous namespace

// Classify VT as either 32 or 64 bit.
static bool is32Bit(EVT VT) {
  switch (VT.getSimpleVT().SimpleTy) {
  case MVT::i32:
    return true;
  case MVT::i64:
    return false;
  default:
    llvm_unreachable("Unsupported type");
  }
}

// Return a version of MachineOperand that can be safely used before the
// final use.
static MachineOperand earlyUseOperand(MachineOperand Op) {
  if (Op.isReg())
    Op.setIsKill(false);
  return Op;
}

SystemZTargetLowering::SystemZTargetLowering(const TargetMachine &TM,
                                             const SystemZSubtarget &STI)
    : TargetLowering(TM), Subtarget(STI) {
  MVT PtrVT = MVT::getIntegerVT(8 * TM.getPointerSize());

  // Set up the register classes.
  if (Subtarget.hasHighWord())
    addRegisterClass(MVT::i32, &SystemZ::GRX32BitRegClass);
  else
    addRegisterClass(MVT::i32, &SystemZ::GR32BitRegClass);
  addRegisterClass(MVT::i64, &SystemZ::GR64BitRegClass);
  if (Subtarget.hasVector()) {
    addRegisterClass(MVT::f32, &SystemZ::VR32BitRegClass);
    addRegisterClass(MVT::f64, &SystemZ::VR64BitRegClass);
  } else {
    addRegisterClass(MVT::f32, &SystemZ::FP32BitRegClass);
    addRegisterClass(MVT::f64, &SystemZ::FP64BitRegClass);
  }
  addRegisterClass(MVT::f128, &SystemZ::FP128BitRegClass);

  if (Subtarget.hasVector()) {
    addRegisterClass(MVT::v16i8, &SystemZ::VR128BitRegClass);
    addRegisterClass(MVT::v8i16, &SystemZ::VR128BitRegClass);
    addRegisterClass(MVT::v4i32, &SystemZ::VR128BitRegClass);
    addRegisterClass(MVT::v2i64, &SystemZ::VR128BitRegClass);
    addRegisterClass(MVT::v4f32, &SystemZ::VR128BitRegClass);
    addRegisterClass(MVT::v2f64, &SystemZ::VR128BitRegClass);
  }

  // Compute derived properties from the register classes
  computeRegisterProperties(Subtarget.getRegisterInfo());

  // Set up special registers.
  setStackPointerRegisterToSaveRestore(SystemZ::R15D);

  // TODO: It may be better to default to latency-oriented scheduling, however
  // LLVM's current latency-oriented scheduler can't handle physreg definitions
  // such as SystemZ has with CC, so set this to the register-pressure
  // scheduler, because it can.
  setSchedulingPreference(Sched::RegPressure);

  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);

  // Instructions are strings of 2-byte aligned 2-byte values.
  setMinFunctionAlignment(2);

  // Handle operations that are handled in a similar way for all types.
  for (unsigned I = MVT::FIRST_INTEGER_VALUETYPE;
       I <= MVT::LAST_FP_VALUETYPE;
       ++I) {
    MVT VT = MVT::SimpleValueType(I);
    if (isTypeLegal(VT)) {
      // Lower SET_CC into an IPM-based sequence.
      setOperationAction(ISD::SETCC, VT, Custom);

      // Expand SELECT(C, A, B) into SELECT_CC(X, 0, A, B, NE).
      setOperationAction(ISD::SELECT, VT, Expand);

      // Lower SELECT_CC and BR_CC into separate comparisons and branches.
      setOperationAction(ISD::SELECT_CC, VT, Custom);
      setOperationAction(ISD::BR_CC,     VT, Custom);
    }
  }

  // Expand jump table branches as address arithmetic followed by an
  // indirect jump.
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  // Expand BRCOND into a BR_CC (see above).
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);

  // Handle integer types.
  for (unsigned I = MVT::FIRST_INTEGER_VALUETYPE;
       I <= MVT::LAST_INTEGER_VALUETYPE;
       ++I) {
    MVT VT = MVT::SimpleValueType(I);
    if (isTypeLegal(VT)) {
      // Expand individual DIV and REMs into DIVREMs.
      setOperationAction(ISD::SDIV, VT, Expand);
      setOperationAction(ISD::UDIV, VT, Expand);
      setOperationAction(ISD::SREM, VT, Expand);
      setOperationAction(ISD::UREM, VT, Expand);
      setOperationAction(ISD::SDIVREM, VT, Custom);
      setOperationAction(ISD::UDIVREM, VT, Custom);

      // Lower ATOMIC_LOAD and ATOMIC_STORE into normal volatile loads and
      // stores, putting a serialization instruction after the stores.
      setOperationAction(ISD::ATOMIC_LOAD,  VT, Custom);
      setOperationAction(ISD::ATOMIC_STORE, VT, Custom);

      // Lower ATOMIC_LOAD_SUB into ATOMIC_LOAD_ADD if LAA and LAAG are
      // available, or if the operand is constant.
      setOperationAction(ISD::ATOMIC_LOAD_SUB, VT, Custom);

      // Use POPCNT on z196 and above.
      if (Subtarget.hasPopulationCount())
        setOperationAction(ISD::CTPOP, VT, Custom);
      else
        setOperationAction(ISD::CTPOP, VT, Expand);

      // No special instructions for these.
      setOperationAction(ISD::CTTZ,            VT, Expand);
      setOperationAction(ISD::CTTZ_ZERO_UNDEF, VT, Expand);
      setOperationAction(ISD::CTLZ_ZERO_UNDEF, VT, Expand);
      setOperationAction(ISD::ROTR,            VT, Expand);

      // Use *MUL_LOHI where possible instead of MULH*.
      setOperationAction(ISD::MULHS, VT, Expand);
      setOperationAction(ISD::MULHU, VT, Expand);
      setOperationAction(ISD::SMUL_LOHI, VT, Custom);
      setOperationAction(ISD::UMUL_LOHI, VT, Custom);

      // Only z196 and above have native support for conversions to unsigned.
      if (!Subtarget.hasFPExtension())
        setOperationAction(ISD::FP_TO_UINT, VT, Expand);
    }
  }

  // Type legalization will convert 8- and 16-bit atomic operations into
  // forms that operate on i32s (but still keeping the original memory VT).
  // Lower them into full i32 operations.
  setOperationAction(ISD::ATOMIC_SWAP,      MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_ADD,  MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_SUB,  MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_AND,  MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_OR,   MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_XOR,  MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_NAND, MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_MIN,  MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_MAX,  MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_UMIN, MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_LOAD_UMAX, MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_CMP_SWAP,  MVT::i32, Custom);

  // z10 has instructions for signed but not unsigned FP conversion.
  // Handle unsigned 32-bit types as signed 64-bit types.
  if (!Subtarget.hasFPExtension()) {
    setOperationAction(ISD::UINT_TO_FP, MVT::i32, Promote);
    setOperationAction(ISD::UINT_TO_FP, MVT::i64, Expand);
  }

  // We have native support for a 64-bit CTLZ, via FLOGR.
  setOperationAction(ISD::CTLZ, MVT::i32, Promote);
  setOperationAction(ISD::CTLZ, MVT::i64, Legal);

  // Give LowerOperation the chance to replace 64-bit ORs with subregs.
  setOperationAction(ISD::OR, MVT::i64, Custom);

  // FIXME: Can we support these natively?
  setOperationAction(ISD::SRL_PARTS, MVT::i64, Expand);
  setOperationAction(ISD::SHL_PARTS, MVT::i64, Expand);
  setOperationAction(ISD::SRA_PARTS, MVT::i64, Expand);

  // We have native instructions for i8, i16 and i32 extensions, but not i1.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::EXTLOAD,  VT, MVT::i1, Promote);
  }

  // Handle the various types of symbolic address.
  setOperationAction(ISD::ConstantPool,     PtrVT, Custom);
  setOperationAction(ISD::GlobalAddress,    PtrVT, Custom);
  setOperationAction(ISD::GlobalTLSAddress, PtrVT, Custom);
  setOperationAction(ISD::BlockAddress,     PtrVT, Custom);
  setOperationAction(ISD::JumpTable,        PtrVT, Custom);

  // We need to handle dynamic allocations specially because of the
  // 160-byte area at the bottom of the stack.
  setOperationAction(ISD::DYNAMIC_STACKALLOC, PtrVT, Custom);

  // Use custom expanders so that we can force the function to use
  // a frame pointer.
  setOperationAction(ISD::STACKSAVE,    MVT::Other, Custom);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Custom);

  // Handle prefetches with PFD or PFDRL.
  setOperationAction(ISD::PREFETCH, MVT::Other, Custom);

  for (MVT VT : MVT::vector_valuetypes()) {
    // Assume by default that all vector operations need to be expanded.
    for (unsigned Opcode = 0; Opcode < ISD::BUILTIN_OP_END; ++Opcode)
      if (getOperationAction(Opcode, VT) == Legal)
        setOperationAction(Opcode, VT, Expand);

    // Likewise all truncating stores and extending loads.
    for (MVT InnerVT : MVT::vector_valuetypes()) {
      setTruncStoreAction(VT, InnerVT, Expand);
      setLoadExtAction(ISD::SEXTLOAD, VT, InnerVT, Expand);
      setLoadExtAction(ISD::ZEXTLOAD, VT, InnerVT, Expand);
      setLoadExtAction(ISD::EXTLOAD, VT, InnerVT, Expand);
    }

    if (isTypeLegal(VT)) {
      // These operations are legal for anything that can be stored in a
      // vector register, even if there is no native support for the format
      // as such.  In particular, we can do these for v4f32 even though there
      // are no specific instructions for that format.
      setOperationAction(ISD::LOAD, VT, Legal);
      setOperationAction(ISD::STORE, VT, Legal);
      setOperationAction(ISD::VSELECT, VT, Legal);
      setOperationAction(ISD::BITCAST, VT, Legal);
      setOperationAction(ISD::UNDEF, VT, Legal);

      // Likewise, except that we need to replace the nodes with something
      // more specific.
      setOperationAction(ISD::BUILD_VECTOR, VT, Custom);
      setOperationAction(ISD::VECTOR_SHUFFLE, VT, Custom);
    }
  }

  // Handle integer vector types.
  for (MVT VT : MVT::integer_vector_valuetypes()) {
    if (isTypeLegal(VT)) {
      // These operations have direct equivalents.
      setOperationAction(ISD::EXTRACT_VECTOR_ELT, VT, Legal);
      setOperationAction(ISD::INSERT_VECTOR_ELT, VT, Legal);
      setOperationAction(ISD::ADD, VT, Legal);
      setOperationAction(ISD::SUB, VT, Legal);
      if (VT != MVT::v2i64)
        setOperationAction(ISD::MUL, VT, Legal);
      setOperationAction(ISD::AND, VT, Legal);
      setOperationAction(ISD::OR, VT, Legal);
      setOperationAction(ISD::XOR, VT, Legal);
      setOperationAction(ISD::CTPOP, VT, Custom);
      setOperationAction(ISD::CTTZ, VT, Legal);
      setOperationAction(ISD::CTLZ, VT, Legal);
      setOperationAction(ISD::CTTZ_ZERO_UNDEF, VT, Custom);
      setOperationAction(ISD::CTLZ_ZERO_UNDEF, VT, Custom);

      // Convert a GPR scalar to a vector by inserting it into element 0.
      setOperationAction(ISD::SCALAR_TO_VECTOR, VT, Custom);

      // Use a series of unpacks for extensions.
      setOperationAction(ISD::SIGN_EXTEND_VECTOR_INREG, VT, Custom);
      setOperationAction(ISD::ZERO_EXTEND_VECTOR_INREG, VT, Custom);

      // Detect shifts by a scalar amount and convert them into
      // V*_BY_SCALAR.
      setOperationAction(ISD::SHL, VT, Custom);
      setOperationAction(ISD::SRA, VT, Custom);
      setOperationAction(ISD::SRL, VT, Custom);

      // At present ROTL isn't matched by DAGCombiner.  ROTR should be
      // converted into ROTL.
      setOperationAction(ISD::ROTL, VT, Expand);
      setOperationAction(ISD::ROTR, VT, Expand);

      // Map SETCCs onto one of VCE, VCH or VCHL, swapping the operands
      // and inverting the result as necessary.
      setOperationAction(ISD::SETCC, VT, Custom);
    }
  }

  if (Subtarget.hasVector()) {
    // There should be no need to check for float types other than v2f64
    // since <2 x f32> isn't a legal type.
    setOperationAction(ISD::FP_TO_SINT, MVT::v2i64, Legal);
    setOperationAction(ISD::FP_TO_UINT, MVT::v2i64, Legal);
    setOperationAction(ISD::SINT_TO_FP, MVT::v2i64, Legal);
    setOperationAction(ISD::UINT_TO_FP, MVT::v2i64, Legal);
  }

  // Handle floating-point types.
  for (unsigned I = MVT::FIRST_FP_VALUETYPE;
       I <= MVT::LAST_FP_VALUETYPE;
       ++I) {
    MVT VT = MVT::SimpleValueType(I);
    if (isTypeLegal(VT)) {
      // We can use FI for FRINT.
      setOperationAction(ISD::FRINT, VT, Legal);

      // We can use the extended form of FI for other rounding operations.
      if (Subtarget.hasFPExtension()) {
        setOperationAction(ISD::FNEARBYINT, VT, Legal);
        setOperationAction(ISD::FFLOOR, VT, Legal);
        setOperationAction(ISD::FCEIL, VT, Legal);
        setOperationAction(ISD::FTRUNC, VT, Legal);
        setOperationAction(ISD::FROUND, VT, Legal);
      }

      // No special instructions for these.
      setOperationAction(ISD::FSIN, VT, Expand);
      setOperationAction(ISD::FCOS, VT, Expand);
      setOperationAction(ISD::FSINCOS, VT, Expand);
      setOperationAction(ISD::FREM, VT, Expand);
      setOperationAction(ISD::FPOW, VT, Expand);
    }
  }

  // Handle floating-point vector types.
  if (Subtarget.hasVector()) {
    // Scalar-to-vector conversion is just a subreg.
    setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v4f32, Legal);
    setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v2f64, Legal);

    // Some insertions and extractions can be done directly but others
    // need to go via integers.
    setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v4f32, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v2f64, Custom);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v4f32, Custom);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v2f64, Custom);

    // These operations have direct equivalents.
    setOperationAction(ISD::FADD, MVT::v2f64, Legal);
    setOperationAction(ISD::FNEG, MVT::v2f64, Legal);
    setOperationAction(ISD::FSUB, MVT::v2f64, Legal);
    setOperationAction(ISD::FMUL, MVT::v2f64, Legal);
    setOperationAction(ISD::FMA, MVT::v2f64, Legal);
    setOperationAction(ISD::FDIV, MVT::v2f64, Legal);
    setOperationAction(ISD::FABS, MVT::v2f64, Legal);
    setOperationAction(ISD::FSQRT, MVT::v2f64, Legal);
    setOperationAction(ISD::FRINT, MVT::v2f64, Legal);
    setOperationAction(ISD::FNEARBYINT, MVT::v2f64, Legal);
    setOperationAction(ISD::FFLOOR, MVT::v2f64, Legal);
    setOperationAction(ISD::FCEIL, MVT::v2f64, Legal);
    setOperationAction(ISD::FTRUNC, MVT::v2f64, Legal);
    setOperationAction(ISD::FROUND, MVT::v2f64, Legal);
  }

  // We have fused multiply-addition for f32 and f64 but not f128.
  setOperationAction(ISD::FMA, MVT::f32,  Legal);
  setOperationAction(ISD::FMA, MVT::f64,  Legal);
  setOperationAction(ISD::FMA, MVT::f128, Expand);

  // Needed so that we don't try to implement f128 constant loads using
  // a load-and-extend of a f80 constant (in cases where the constant
  // would fit in an f80).
  for (MVT VT : MVT::fp_valuetypes())
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f80, Expand);

  // Floating-point truncation and stores need to be done separately.
  setTruncStoreAction(MVT::f64,  MVT::f32, Expand);
  setTruncStoreAction(MVT::f128, MVT::f32, Expand);
  setTruncStoreAction(MVT::f128, MVT::f64, Expand);

  // We have 64-bit FPR<->GPR moves, but need special handling for
  // 32-bit forms.
  if (!Subtarget.hasVector()) {
    setOperationAction(ISD::BITCAST, MVT::i32, Custom);
    setOperationAction(ISD::BITCAST, MVT::f32, Custom);
  }

  // VASTART and VACOPY need to deal with the SystemZ-specific varargs
  // structure, but VAEND is a no-op.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VACOPY,  MVT::Other, Custom);
  setOperationAction(ISD::VAEND,   MVT::Other, Expand);

  // Codes for which we want to perform some z-specific combinations.
  setTargetDAGCombine(ISD::SIGN_EXTEND);
  setTargetDAGCombine(ISD::STORE);
  setTargetDAGCombine(ISD::EXTRACT_VECTOR_ELT);
  setTargetDAGCombine(ISD::FP_ROUND);

  // Handle intrinsics.
  setOperationAction(ISD::INTRINSIC_W_CHAIN, MVT::Other, Custom);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);

  // We want to use MVC in preference to even a single load/store pair.
  MaxStoresPerMemcpy = 0;
  MaxStoresPerMemcpyOptSize = 0;

  // The main memset sequence is a byte store followed by an MVC.
  // Two STC or MV..I stores win over that, but the kind of fused stores
  // generated by target-independent code don't when the byte value is
  // variable.  E.g.  "STC <reg>;MHI <reg>,257;STH <reg>" is not better
  // than "STC;MVC".  Handle the choice in target-specific code instead.
  MaxStoresPerMemset = 0;
  MaxStoresPerMemsetOptSize = 0;
}

EVT SystemZTargetLowering::getSetCCResultType(const DataLayout &DL,
                                              LLVMContext &, EVT VT) const {
  if (!VT.isVector())
    return MVT::i32;
  return VT.changeVectorElementTypeToInteger();
}

bool SystemZTargetLowering::isFMAFasterThanFMulAndFAdd(EVT VT) const {
  VT = VT.getScalarType();

  if (!VT.isSimple())
    return false;

  switch (VT.getSimpleVT().SimpleTy) {
  case MVT::f32:
  case MVT::f64:
    return true;
  case MVT::f128:
    return false;
  default:
    break;
  }

  return false;
}

bool SystemZTargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT) const {
  // We can load zero using LZ?R and negative zero using LZ?R;LC?BR.
  return Imm.isZero() || Imm.isNegZero();
}

bool SystemZTargetLowering::isLegalICmpImmediate(int64_t Imm) const {
  // We can use CGFI or CLGFI.
  return isInt<32>(Imm) || isUInt<32>(Imm);
}

bool SystemZTargetLowering::isLegalAddImmediate(int64_t Imm) const {
  // We can use ALGFI or SLGFI.
  return isUInt<32>(Imm) || isUInt<32>(-Imm);
}

bool SystemZTargetLowering::allowsMisalignedMemoryAccesses(EVT VT,
                                                           unsigned,
                                                           unsigned,
                                                           bool *Fast) const {
  // Unaligned accesses should never be slower than the expanded version.
  // We check specifically for aligned accesses in the few cases where
  // they are required.
  if (Fast)
    *Fast = true;
  return true;
}

bool SystemZTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                                  const AddrMode &AM, Type *Ty,
                                                  unsigned AS) const {
  // Punt on globals for now, although they can be used in limited
  // RELATIVE LONG cases.
  if (AM.BaseGV)
    return false;

  // Require a 20-bit signed offset.
  if (!isInt<20>(AM.BaseOffs))
    return false;

  // Indexing is OK but no scale factor can be applied.
  return AM.Scale == 0 || AM.Scale == 1;
}

bool SystemZTargetLowering::isTruncateFree(Type *FromType, Type *ToType) const {
  if (!FromType->isIntegerTy() || !ToType->isIntegerTy())
    return false;
  unsigned FromBits = FromType->getPrimitiveSizeInBits();
  unsigned ToBits = ToType->getPrimitiveSizeInBits();
  return FromBits > ToBits;
}

bool SystemZTargetLowering::isTruncateFree(EVT FromVT, EVT ToVT) const {
  if (!FromVT.isInteger() || !ToVT.isInteger())
    return false;
  unsigned FromBits = FromVT.getSizeInBits();
  unsigned ToBits = ToVT.getSizeInBits();
  return FromBits > ToBits;
}

//===----------------------------------------------------------------------===//
// Inline asm support
//===----------------------------------------------------------------------===//

TargetLowering::ConstraintType
SystemZTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'a': // Address register
    case 'd': // Data register (equivalent to 'r')
    case 'f': // Floating-point register
    case 'h': // High-part register
    case 'r': // General-purpose register
      return C_RegisterClass;

    case 'Q': // Memory with base and unsigned 12-bit displacement
    case 'R': // Likewise, plus an index
    case 'S': // Memory with base and signed 20-bit displacement
    case 'T': // Likewise, plus an index
    case 'm': // Equivalent to 'T'.
      return C_Memory;

    case 'I': // Unsigned 8-bit constant
    case 'J': // Unsigned 12-bit constant
    case 'K': // Signed 16-bit constant
    case 'L': // Signed 20-bit displacement (on all targets we support)
    case 'M': // 0x7fffffff
      return C_Other;

    default:
      break;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

TargetLowering::ConstraintWeight SystemZTargetLowering::
getSingleConstraintMatchWeight(AsmOperandInfo &info,
                               const char *constraint) const {
  ConstraintWeight weight = CW_Invalid;
  Value *CallOperandVal = info.CallOperandVal;
  // If we don't have a value, we can't do a match,
  // but allow it at the lowest weight.
  if (!CallOperandVal)
    return CW_Default;
  Type *type = CallOperandVal->getType();
  // Look at the constraint type.
  switch (*constraint) {
  default:
    weight = TargetLowering::getSingleConstraintMatchWeight(info, constraint);
    break;

  case 'a': // Address register
  case 'd': // Data register (equivalent to 'r')
  case 'h': // High-part register
  case 'r': // General-purpose register
    if (CallOperandVal->getType()->isIntegerTy())
      weight = CW_Register;
    break;

  case 'f': // Floating-point register
    if (type->isFloatingPointTy())
      weight = CW_Register;
    break;

  case 'I': // Unsigned 8-bit constant
    if (auto *C = dyn_cast<ConstantInt>(CallOperandVal))
      if (isUInt<8>(C->getZExtValue()))
        weight = CW_Constant;
    break;

  case 'J': // Unsigned 12-bit constant
    if (auto *C = dyn_cast<ConstantInt>(CallOperandVal))
      if (isUInt<12>(C->getZExtValue()))
        weight = CW_Constant;
    break;

  case 'K': // Signed 16-bit constant
    if (auto *C = dyn_cast<ConstantInt>(CallOperandVal))
      if (isInt<16>(C->getSExtValue()))
        weight = CW_Constant;
    break;

  case 'L': // Signed 20-bit displacement (on all targets we support)
    if (auto *C = dyn_cast<ConstantInt>(CallOperandVal))
      if (isInt<20>(C->getSExtValue()))
        weight = CW_Constant;
    break;

  case 'M': // 0x7fffffff
    if (auto *C = dyn_cast<ConstantInt>(CallOperandVal))
      if (C->getZExtValue() == 0x7fffffff)
        weight = CW_Constant;
    break;
  }
  return weight;
}

// Parse a "{tNNN}" register constraint for which the register type "t"
// has already been verified.  MC is the class associated with "t" and
// Map maps 0-based register numbers to LLVM register numbers.
static std::pair<unsigned, const TargetRegisterClass *>
parseRegisterNumber(StringRef Constraint, const TargetRegisterClass *RC,
                    const unsigned *Map) {
  assert(*(Constraint.end()-1) == '}' && "Missing '}'");
  if (isdigit(Constraint[2])) {
    unsigned Index;
    bool Failed =
        Constraint.slice(2, Constraint.size() - 1).getAsInteger(10, Index);
    if (!Failed && Index < 16 && Map[Index])
      return std::make_pair(Map[Index], RC);
  }
  return std::make_pair(0U, nullptr);
}

std::pair<unsigned, const TargetRegisterClass *>
SystemZTargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef Constraint, MVT VT) const {
  if (Constraint.size() == 1) {
    // GCC Constraint Letters
    switch (Constraint[0]) {
    default: break;
    case 'd': // Data register (equivalent to 'r')
    case 'r': // General-purpose register
      if (VT == MVT::i64)
        return std::make_pair(0U, &SystemZ::GR64BitRegClass);
      else if (VT == MVT::i128)
        return std::make_pair(0U, &SystemZ::GR128BitRegClass);
      return std::make_pair(0U, &SystemZ::GR32BitRegClass);

    case 'a': // Address register
      if (VT == MVT::i64)
        return std::make_pair(0U, &SystemZ::ADDR64BitRegClass);
      else if (VT == MVT::i128)
        return std::make_pair(0U, &SystemZ::ADDR128BitRegClass);
      return std::make_pair(0U, &SystemZ::ADDR32BitRegClass);

    case 'h': // High-part register (an LLVM extension)
      return std::make_pair(0U, &SystemZ::GRH32BitRegClass);

    case 'f': // Floating-point register
      if (VT == MVT::f64)
        return std::make_pair(0U, &SystemZ::FP64BitRegClass);
      else if (VT == MVT::f128)
        return std::make_pair(0U, &SystemZ::FP128BitRegClass);
      return std::make_pair(0U, &SystemZ::FP32BitRegClass);
    }
  }
  if (Constraint.size() > 0 && Constraint[0] == '{') {
    // We need to override the default register parsing for GPRs and FPRs
    // because the interpretation depends on VT.  The internal names of
    // the registers are also different from the external names
    // (F0D and F0S instead of F0, etc.).
    if (Constraint[1] == 'r') {
      if (VT == MVT::i32)
        return parseRegisterNumber(Constraint, &SystemZ::GR32BitRegClass,
                                   SystemZMC::GR32Regs);
      if (VT == MVT::i128)
        return parseRegisterNumber(Constraint, &SystemZ::GR128BitRegClass,
                                   SystemZMC::GR128Regs);
      return parseRegisterNumber(Constraint, &SystemZ::GR64BitRegClass,
                                 SystemZMC::GR64Regs);
    }
    if (Constraint[1] == 'f') {
      if (VT == MVT::f32)
        return parseRegisterNumber(Constraint, &SystemZ::FP32BitRegClass,
                                   SystemZMC::FP32Regs);
      if (VT == MVT::f128)
        return parseRegisterNumber(Constraint, &SystemZ::FP128BitRegClass,
                                   SystemZMC::FP128Regs);
      return parseRegisterNumber(Constraint, &SystemZ::FP64BitRegClass,
                                 SystemZMC::FP64Regs);
    }
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

void SystemZTargetLowering::
LowerAsmOperandForConstraint(SDValue Op, std::string &Constraint,
                             std::vector<SDValue> &Ops,
                             SelectionDAG &DAG) const {
  // Only support length 1 constraints for now.
  if (Constraint.length() == 1) {
    switch (Constraint[0]) {
    case 'I': // Unsigned 8-bit constant
      if (auto *C = dyn_cast<ConstantSDNode>(Op))
        if (isUInt<8>(C->getZExtValue()))
          Ops.push_back(DAG.getTargetConstant(C->getZExtValue(), SDLoc(Op),
                                              Op.getValueType()));
      return;

    case 'J': // Unsigned 12-bit constant
      if (auto *C = dyn_cast<ConstantSDNode>(Op))
        if (isUInt<12>(C->getZExtValue()))
          Ops.push_back(DAG.getTargetConstant(C->getZExtValue(), SDLoc(Op),
                                              Op.getValueType()));
      return;

    case 'K': // Signed 16-bit constant
      if (auto *C = dyn_cast<ConstantSDNode>(Op))
        if (isInt<16>(C->getSExtValue()))
          Ops.push_back(DAG.getTargetConstant(C->getSExtValue(), SDLoc(Op),
                                              Op.getValueType()));
      return;

    case 'L': // Signed 20-bit displacement (on all targets we support)
      if (auto *C = dyn_cast<ConstantSDNode>(Op))
        if (isInt<20>(C->getSExtValue()))
          Ops.push_back(DAG.getTargetConstant(C->getSExtValue(), SDLoc(Op),
                                              Op.getValueType()));
      return;

    case 'M': // 0x7fffffff
      if (auto *C = dyn_cast<ConstantSDNode>(Op))
        if (C->getZExtValue() == 0x7fffffff)
          Ops.push_back(DAG.getTargetConstant(C->getZExtValue(), SDLoc(Op),
                                              Op.getValueType()));
      return;
    }
  }
  TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

//===----------------------------------------------------------------------===//
// Calling conventions
//===----------------------------------------------------------------------===//

#include "SystemZGenCallingConv.inc"

bool SystemZTargetLowering::allowTruncateForTailCall(Type *FromType,
                                                     Type *ToType) const {
  return isTruncateFree(FromType, ToType);
}

bool SystemZTargetLowering::mayBeEmittedAsTailCall(CallInst *CI) const {
  return CI->isTailCall();
}

// We do not yet support 128-bit single-element vector types.  If the user
// attempts to use such types as function argument or return type, prefer
// to error out instead of emitting code violating the ABI.
static void VerifyVectorType(MVT VT, EVT ArgVT) {
  if (ArgVT.isVector() && !VT.isVector())
    report_fatal_error("Unsupported vector argument or return type");
}

static void VerifyVectorTypes(const SmallVectorImpl<ISD::InputArg> &Ins) {
  for (unsigned i = 0; i < Ins.size(); ++i)
    VerifyVectorType(Ins[i].VT, Ins[i].ArgVT);
}

static void VerifyVectorTypes(const SmallVectorImpl<ISD::OutputArg> &Outs) {
  for (unsigned i = 0; i < Outs.size(); ++i)
    VerifyVectorType(Outs[i].VT, Outs[i].ArgVT);
}

// Value is a value that has been passed to us in the location described by VA
// (and so has type VA.getLocVT()).  Convert Value to VA.getValVT(), chaining
// any loads onto Chain.
static SDValue convertLocVTToValVT(SelectionDAG &DAG, SDLoc DL,
                                   CCValAssign &VA, SDValue Chain,
                                   SDValue Value) {
  // If the argument has been promoted from a smaller type, insert an
  // assertion to capture this.
  if (VA.getLocInfo() == CCValAssign::SExt)
    Value = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), Value,
                        DAG.getValueType(VA.getValVT()));
  else if (VA.getLocInfo() == CCValAssign::ZExt)
    Value = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), Value,
                        DAG.getValueType(VA.getValVT()));

  if (VA.isExtInLoc())
    Value = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Value);
  else if (VA.getLocInfo() == CCValAssign::Indirect)
    Value = DAG.getLoad(VA.getValVT(), DL, Chain, Value,
                        MachinePointerInfo(), false, false, false, 0);
  else if (VA.getLocInfo() == CCValAssign::BCvt) {
    // If this is a short vector argument loaded from the stack,
    // extend from i64 to full vector size and then bitcast.
    assert(VA.getLocVT() == MVT::i64);
    assert(VA.getValVT().isVector());
    Value = DAG.getNode(ISD::BUILD_VECTOR, DL, MVT::v2i64,
                        Value, DAG.getUNDEF(MVT::i64));
    Value = DAG.getNode(ISD::BITCAST, DL, VA.getValVT(), Value);
  } else
    assert(VA.getLocInfo() == CCValAssign::Full && "Unsupported getLocInfo");
  return Value;
}

// Value is a value of type VA.getValVT() that we need to copy into
// the location described by VA.  Return a copy of Value converted to
// VA.getValVT().  The caller is responsible for handling indirect values.
static SDValue convertValVTToLocVT(SelectionDAG &DAG, SDLoc DL,
                                   CCValAssign &VA, SDValue Value) {
  switch (VA.getLocInfo()) {
  case CCValAssign::SExt:
    return DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Value);
  case CCValAssign::ZExt:
    return DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Value);
  case CCValAssign::AExt:
    return DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Value);
  case CCValAssign::BCvt:
    // If this is a short vector argument to be stored to the stack,
    // bitcast to v2i64 and then extract first element.
    assert(VA.getLocVT() == MVT::i64);
    assert(VA.getValVT().isVector());
    Value = DAG.getNode(ISD::BITCAST, DL, MVT::v2i64, Value);
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VA.getLocVT(), Value,
                       DAG.getConstant(0, DL, MVT::i32));
  case CCValAssign::Full:
    return Value;
  default:
    llvm_unreachable("Unhandled getLocInfo()");
  }
}

SDValue SystemZTargetLowering::
LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                     const SmallVectorImpl<ISD::InputArg> &Ins,
                     SDLoc DL, SelectionDAG &DAG,
                     SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SystemZMachineFunctionInfo *FuncInfo =
      MF.getInfo<SystemZMachineFunctionInfo>();
  auto *TFL =
      static_cast<const SystemZFrameLowering *>(Subtarget.getFrameLowering());

  // Detect unsupported vector argument types.
  if (Subtarget.hasVector())
    VerifyVectorTypes(Ins);

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  SystemZCCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_SystemZ);

  unsigned NumFixedGPRs = 0;
  unsigned NumFixedFPRs = 0;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    SDValue ArgValue;
    CCValAssign &VA = ArgLocs[I];
    EVT LocVT = VA.getLocVT();
    if (VA.isRegLoc()) {
      // Arguments passed in registers
      const TargetRegisterClass *RC;
      switch (LocVT.getSimpleVT().SimpleTy) {
      default:
        // Integers smaller than i64 should be promoted to i64.
        llvm_unreachable("Unexpected argument type");
      case MVT::i32:
        NumFixedGPRs += 1;
        RC = &SystemZ::GR32BitRegClass;
        break;
      case MVT::i64:
        NumFixedGPRs += 1;
        RC = &SystemZ::GR64BitRegClass;
        break;
      case MVT::f32:
        NumFixedFPRs += 1;
        RC = &SystemZ::FP32BitRegClass;
        break;
      case MVT::f64:
        NumFixedFPRs += 1;
        RC = &SystemZ::FP64BitRegClass;
        break;
      case MVT::v16i8:
      case MVT::v8i16:
      case MVT::v4i32:
      case MVT::v2i64:
      case MVT::v4f32:
      case MVT::v2f64:
        RC = &SystemZ::VR128BitRegClass;
        break;
      }

      unsigned VReg = MRI.createVirtualRegister(RC);
      MRI.addLiveIn(VA.getLocReg(), VReg);
      ArgValue = DAG.getCopyFromReg(Chain, DL, VReg, LocVT);
    } else {
      assert(VA.isMemLoc() && "Argument not register or memory");

      // Create the frame index object for this incoming parameter.
      int FI = MFI->CreateFixedObject(LocVT.getSizeInBits() / 8,
                                      VA.getLocMemOffset(), true);

      // Create the SelectionDAG nodes corresponding to a load
      // from this parameter.  Unpromoted ints and floats are
      // passed as right-justified 8-byte values.
      EVT PtrVT = getPointerTy(DAG.getDataLayout());
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      if (VA.getLocVT() == MVT::i32 || VA.getLocVT() == MVT::f32)
        FIN = DAG.getNode(ISD::ADD, DL, PtrVT, FIN,
                          DAG.getIntPtrConstant(4, DL));
      ArgValue = DAG.getLoad(LocVT, DL, Chain, FIN,
                             MachinePointerInfo::getFixedStack(MF, FI), false,
                             false, false, 0);
    }

    // Convert the value of the argument register into the value that's
    // being passed.
    InVals.push_back(convertLocVTToValVT(DAG, DL, VA, Chain, ArgValue));
  }

  if (IsVarArg) {
    // Save the number of non-varargs registers for later use by va_start, etc.
    FuncInfo->setVarArgsFirstGPR(NumFixedGPRs);
    FuncInfo->setVarArgsFirstFPR(NumFixedFPRs);

    // Likewise the address (in the form of a frame index) of where the
    // first stack vararg would be.  The 1-byte size here is arbitrary.
    int64_t StackSize = CCInfo.getNextStackOffset();
    FuncInfo->setVarArgsFrameIndex(MFI->CreateFixedObject(1, StackSize, true));

    // ...and a similar frame index for the caller-allocated save area
    // that will be used to store the incoming registers.
    int64_t RegSaveOffset = TFL->getOffsetOfLocalArea();
    unsigned RegSaveIndex = MFI->CreateFixedObject(1, RegSaveOffset, true);
    FuncInfo->setRegSaveFrameIndex(RegSaveIndex);

    // Store the FPR varargs in the reserved frame slots.  (We store the
    // GPRs as part of the prologue.)
    if (NumFixedFPRs < SystemZ::NumArgFPRs) {
      SDValue MemOps[SystemZ::NumArgFPRs];
      for (unsigned I = NumFixedFPRs; I < SystemZ::NumArgFPRs; ++I) {
        unsigned Offset = TFL->getRegSpillOffset(SystemZ::ArgFPRs[I]);
        int FI = MFI->CreateFixedObject(8, RegSaveOffset + Offset, true);
        SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
        unsigned VReg = MF.addLiveIn(SystemZ::ArgFPRs[I],
                                     &SystemZ::FP64BitRegClass);
        SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, VReg, MVT::f64);
        MemOps[I] = DAG.getStore(ArgValue.getValue(1), DL, ArgValue, FIN,
                                 MachinePointerInfo::getFixedStack(MF, FI),
                                 false, false, 0);
      }
      // Join the stores, which are independent of one another.
      Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other,
                          makeArrayRef(&MemOps[NumFixedFPRs],
                                       SystemZ::NumArgFPRs-NumFixedFPRs));
    }
  }

  return Chain;
}

static bool canUseSiblingCall(const CCState &ArgCCInfo,
                              SmallVectorImpl<CCValAssign> &ArgLocs) {
  // Punt if there are any indirect or stack arguments, or if the call
  // needs the call-saved argument register R6.
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    CCValAssign &VA = ArgLocs[I];
    if (VA.getLocInfo() == CCValAssign::Indirect)
      return false;
    if (!VA.isRegLoc())
      return false;
    unsigned Reg = VA.getLocReg();
    if (Reg == SystemZ::R6H || Reg == SystemZ::R6L || Reg == SystemZ::R6D)
      return false;
  }
  return true;
}

SDValue
SystemZTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                 SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &IsTailCall = CLI.IsTailCall;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;
  MachineFunction &MF = DAG.getMachineFunction();
  EVT PtrVT = getPointerTy(MF.getDataLayout());

  // Detect unsupported vector argument and return types.
  if (Subtarget.hasVector()) {
    VerifyVectorTypes(Outs);
    VerifyVectorTypes(Ins);
  }

  // Analyze the operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  SystemZCCState ArgCCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  ArgCCInfo.AnalyzeCallOperands(Outs, CC_SystemZ);

  // We don't support GuaranteedTailCallOpt, only automatically-detected
  // sibling calls.
  if (IsTailCall && !canUseSiblingCall(ArgCCInfo, ArgLocs))
    IsTailCall = false;

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = ArgCCInfo.getNextStackOffset();

  // Mark the start of the call.
  if (!IsTailCall)
    Chain = DAG.getCALLSEQ_START(Chain,
                                 DAG.getConstant(NumBytes, DL, PtrVT, true),
                                 DL);

  // Copy argument values to their designated locations.
  SmallVector<std::pair<unsigned, SDValue>, 9> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  SDValue StackPtr;
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    CCValAssign &VA = ArgLocs[I];
    SDValue ArgValue = OutVals[I];

    if (VA.getLocInfo() == CCValAssign::Indirect) {
      // Store the argument in a stack slot and pass its address.
      SDValue SpillSlot = DAG.CreateStackTemporary(VA.getValVT());
      int FI = cast<FrameIndexSDNode>(SpillSlot)->getIndex();
      MemOpChains.push_back(DAG.getStore(
          Chain, DL, ArgValue, SpillSlot,
          MachinePointerInfo::getFixedStack(MF, FI), false, false, 0));
      ArgValue = SpillSlot;
    } else
      ArgValue = convertValVTToLocVT(DAG, DL, VA, ArgValue);

    if (VA.isRegLoc())
      // Queue up the argument copies and emit them at the end.
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), ArgValue));
    else {
      assert(VA.isMemLoc() && "Argument not register or memory");

      // Work out the address of the stack slot.  Unpromoted ints and
      // floats are passed as right-justified 8-byte values.
      if (!StackPtr.getNode())
        StackPtr = DAG.getCopyFromReg(Chain, DL, SystemZ::R15D, PtrVT);
      unsigned Offset = SystemZMC::CallFrameSize + VA.getLocMemOffset();
      if (VA.getLocVT() == MVT::i32 || VA.getLocVT() == MVT::f32)
        Offset += 4;
      SDValue Address = DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr,
                                    DAG.getIntPtrConstant(Offset, DL));

      // Emit the store.
      MemOpChains.push_back(DAG.getStore(Chain, DL, ArgValue, Address,
                                         MachinePointerInfo(),
                                         false, false, 0));
    }
  }

  // Join the stores, which are independent of one another.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  // Accept direct calls by converting symbolic call addresses to the
  // associated Target* opcodes.  Force %r1 to be used for indirect
  // tail calls.
  SDValue Glue;
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, PtrVT);
    Callee = DAG.getNode(SystemZISD::PCREL_WRAPPER, DL, PtrVT, Callee);
  } else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), PtrVT);
    Callee = DAG.getNode(SystemZISD::PCREL_WRAPPER, DL, PtrVT, Callee);
  } else if (IsTailCall) {
    Chain = DAG.getCopyToReg(Chain, DL, SystemZ::R1D, Callee, Glue);
    Glue = Chain.getValue(1);
    Callee = DAG.getRegister(SystemZ::R1D, Callee.getValueType());
  }

  // Build a sequence of copy-to-reg nodes, chained and glued together.
  for (unsigned I = 0, E = RegsToPass.size(); I != E; ++I) {
    Chain = DAG.getCopyToReg(Chain, DL, RegsToPass[I].first,
                             RegsToPass[I].second, Glue);
    Glue = Chain.getValue(1);
  }

  // The first call operand is the chain and the second is the target address.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (unsigned I = 0, E = RegsToPass.size(); I != E; ++I)
    Ops.push_back(DAG.getRegister(RegsToPass[I].first,
                                  RegsToPass[I].second.getValueType()));

  // Add a register mask operand representing the call-preserved registers.
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const uint32_t *Mask = TRI->getCallPreservedMask(MF, CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  // Glue the call to the argument copies, if any.
  if (Glue.getNode())
    Ops.push_back(Glue);

  // Emit the call.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  if (IsTailCall)
    return DAG.getNode(SystemZISD::SIBCALL, DL, NodeTys, Ops);
  Chain = DAG.getNode(SystemZISD::CALL, DL, NodeTys, Ops);
  Glue = Chain.getValue(1);

  // Mark the end of the call, which is glued to the call itself.
  Chain = DAG.getCALLSEQ_END(Chain,
                             DAG.getConstant(NumBytes, DL, PtrVT, true),
                             DAG.getConstant(0, DL, PtrVT, true),
                             Glue, DL);
  Glue = Chain.getValue(1);

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RetLocs;
  CCState RetCCInfo(CallConv, IsVarArg, MF, RetLocs, *DAG.getContext());
  RetCCInfo.AnalyzeCallResult(Ins, RetCC_SystemZ);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned I = 0, E = RetLocs.size(); I != E; ++I) {
    CCValAssign &VA = RetLocs[I];

    // Copy the value out, gluing the copy to the end of the call sequence.
    SDValue RetValue = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(),
                                          VA.getLocVT(), Glue);
    Chain = RetValue.getValue(1);
    Glue = RetValue.getValue(2);

    // Convert the value of the return register into the value that's
    // being returned.
    InVals.push_back(convertLocVTToValVT(DAG, DL, VA, Chain, RetValue));
  }

  return Chain;
}

bool SystemZTargetLowering::
CanLowerReturn(CallingConv::ID CallConv,
               MachineFunction &MF, bool isVarArg,
               const SmallVectorImpl<ISD::OutputArg> &Outs,
               LLVMContext &Context) const {
  // Detect unsupported vector return types.
  if (Subtarget.hasVector())
    VerifyVectorTypes(Outs);

  SmallVector<CCValAssign, 16> RetLocs;
  CCState RetCCInfo(CallConv, isVarArg, MF, RetLocs, Context);
  return RetCCInfo.CheckReturn(Outs, RetCC_SystemZ);
}

SDValue
SystemZTargetLowering::LowerReturn(SDValue Chain,
                                   CallingConv::ID CallConv, bool IsVarArg,
                                   const SmallVectorImpl<ISD::OutputArg> &Outs,
                                   const SmallVectorImpl<SDValue> &OutVals,
                                   SDLoc DL, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  // Detect unsupported vector return types.
  if (Subtarget.hasVector())
    VerifyVectorTypes(Outs);

  // Assign locations to each returned value.
  SmallVector<CCValAssign, 16> RetLocs;
  CCState RetCCInfo(CallConv, IsVarArg, MF, RetLocs, *DAG.getContext());
  RetCCInfo.AnalyzeReturn(Outs, RetCC_SystemZ);

  // Quick exit for void returns
  if (RetLocs.empty())
    return DAG.getNode(SystemZISD::RET_FLAG, DL, MVT::Other, Chain);

  // Copy the result values into the output registers.
  SDValue Glue;
  SmallVector<SDValue, 4> RetOps;
  RetOps.push_back(Chain);
  for (unsigned I = 0, E = RetLocs.size(); I != E; ++I) {
    CCValAssign &VA = RetLocs[I];
    SDValue RetValue = OutVals[I];

    // Make the return register live on exit.
    assert(VA.isRegLoc() && "Can only return in registers!");

    // Promote the value as required.
    RetValue = convertValVTToLocVT(DAG, DL, VA, RetValue);

    // Chain and glue the copies together.
    unsigned Reg = VA.getLocReg();
    Chain = DAG.getCopyToReg(Chain, DL, Reg, RetValue, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(Reg, VA.getLocVT()));
  }

  // Update chain and glue.
  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);

  return DAG.getNode(SystemZISD::RET_FLAG, DL, MVT::Other, RetOps);
}

SDValue SystemZTargetLowering::
prepareVolatileOrAtomicLoad(SDValue Chain, SDLoc DL, SelectionDAG &DAG) const {
  return DAG.getNode(SystemZISD::SERIALIZE, DL, MVT::Other, Chain);
}

// Return true if Op is an intrinsic node with chain that returns the CC value
// as its only (other) argument.  Provide the associated SystemZISD opcode and
// the mask of valid CC values if so.
static bool isIntrinsicWithCCAndChain(SDValue Op, unsigned &Opcode,
                                      unsigned &CCValid) {
  unsigned Id = cast<ConstantSDNode>(Op.getOperand(1))->getZExtValue();
  switch (Id) {
  case Intrinsic::s390_tbegin:
    Opcode = SystemZISD::TBEGIN;
    CCValid = SystemZ::CCMASK_TBEGIN;
    return true;

  case Intrinsic::s390_tbegin_nofloat:
    Opcode = SystemZISD::TBEGIN_NOFLOAT;
    CCValid = SystemZ::CCMASK_TBEGIN;
    return true;

  case Intrinsic::s390_tend:
    Opcode = SystemZISD::TEND;
    CCValid = SystemZ::CCMASK_TEND;
    return true;

  default:
    return false;
  }
}

// Return true if Op is an intrinsic node without chain that returns the
// CC value as its final argument.  Provide the associated SystemZISD
// opcode and the mask of valid CC values if so.
static bool isIntrinsicWithCC(SDValue Op, unsigned &Opcode, unsigned &CCValid) {
  unsigned Id = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  switch (Id) {
  case Intrinsic::s390_vpkshs:
  case Intrinsic::s390_vpksfs:
  case Intrinsic::s390_vpksgs:
    Opcode = SystemZISD::PACKS_CC;
    CCValid = SystemZ::CCMASK_VCMP;
    return true;

  case Intrinsic::s390_vpklshs:
  case Intrinsic::s390_vpklsfs:
  case Intrinsic::s390_vpklsgs:
    Opcode = SystemZISD::PACKLS_CC;
    CCValid = SystemZ::CCMASK_VCMP;
    return true;

  case Intrinsic::s390_vceqbs:
  case Intrinsic::s390_vceqhs:
  case Intrinsic::s390_vceqfs:
  case Intrinsic::s390_vceqgs:
    Opcode = SystemZISD::VICMPES;
    CCValid = SystemZ::CCMASK_VCMP;
    return true;

  case Intrinsic::s390_vchbs:
  case Intrinsic::s390_vchhs:
  case Intrinsic::s390_vchfs:
  case Intrinsic::s390_vchgs:
    Opcode = SystemZISD::VICMPHS;
    CCValid = SystemZ::CCMASK_VCMP;
    return true;

  case Intrinsic::s390_vchlbs:
  case Intrinsic::s390_vchlhs:
  case Intrinsic::s390_vchlfs:
  case Intrinsic::s390_vchlgs:
    Opcode = SystemZISD::VICMPHLS;
    CCValid = SystemZ::CCMASK_VCMP;
    return true;

  case Intrinsic::s390_vtm:
    Opcode = SystemZISD::VTM;
    CCValid = SystemZ::CCMASK_VCMP;
    return true;

  case Intrinsic::s390_vfaebs:
  case Intrinsic::s390_vfaehs:
  case Intrinsic::s390_vfaefs:
    Opcode = SystemZISD::VFAE_CC;
    CCValid = SystemZ::CCMASK_ANY;
    return true;

  case Intrinsic::s390_vfaezbs:
  case Intrinsic::s390_vfaezhs:
  case Intrinsic::s390_vfaezfs:
    Opcode = SystemZISD::VFAEZ_CC;
    CCValid = SystemZ::CCMASK_ANY;
    return true;

  case Intrinsic::s390_vfeebs:
  case Intrinsic::s390_vfeehs:
  case Intrinsic::s390_vfeefs:
    Opcode = SystemZISD::VFEE_CC;
    CCValid = SystemZ::CCMASK_ANY;
    return true;

  case Intrinsic::s390_vfeezbs:
  case Intrinsic::s390_vfeezhs:
  case Intrinsic::s390_vfeezfs:
    Opcode = SystemZISD::VFEEZ_CC;
    CCValid = SystemZ::CCMASK_ANY;
    return true;

  case Intrinsic::s390_vfenebs:
  case Intrinsic::s390_vfenehs:
  case Intrinsic::s390_vfenefs:
    Opcode = SystemZISD::VFENE_CC;
    CCValid = SystemZ::CCMASK_ANY;
    return true;

  case Intrinsic::s390_vfenezbs:
  case Intrinsic::s390_vfenezhs:
  case Intrinsic::s390_vfenezfs:
    Opcode = SystemZISD::VFENEZ_CC;
    CCValid = SystemZ::CCMASK_ANY;
    return true;

  case Intrinsic::s390_vistrbs:
  case Intrinsic::s390_vistrhs:
  case Intrinsic::s390_vistrfs:
    Opcode = SystemZISD::VISTR_CC;
    CCValid = SystemZ::CCMASK_0 | SystemZ::CCMASK_3;
    return true;

  case Intrinsic::s390_vstrcbs:
  case Intrinsic::s390_vstrchs:
  case Intrinsic::s390_vstrcfs:
    Opcode = SystemZISD::VSTRC_CC;
    CCValid = SystemZ::CCMASK_ANY;
    return true;

  case Intrinsic::s390_vstrczbs:
  case Intrinsic::s390_vstrczhs:
  case Intrinsic::s390_vstrczfs:
    Opcode = SystemZISD::VSTRCZ_CC;
    CCValid = SystemZ::CCMASK_ANY;
    return true;

  case Intrinsic::s390_vfcedbs:
    Opcode = SystemZISD::VFCMPES;
    CCValid = SystemZ::CCMASK_VCMP;
    return true;

  case Intrinsic::s390_vfchdbs:
    Opcode = SystemZISD::VFCMPHS;
    CCValid = SystemZ::CCMASK_VCMP;
    return true;

  case Intrinsic::s390_vfchedbs:
    Opcode = SystemZISD::VFCMPHES;
    CCValid = SystemZ::CCMASK_VCMP;
    return true;

  case Intrinsic::s390_vftcidb:
    Opcode = SystemZISD::VFTCI;
    CCValid = SystemZ::CCMASK_VCMP;
    return true;

  default:
    return false;
  }
}

// Emit an intrinsic with chain with a glued value instead of its CC result.
static SDValue emitIntrinsicWithChainAndGlue(SelectionDAG &DAG, SDValue Op,
                                             unsigned Opcode) {
  // Copy all operands except the intrinsic ID.
  unsigned NumOps = Op.getNumOperands();
  SmallVector<SDValue, 6> Ops;
  Ops.reserve(NumOps - 1);
  Ops.push_back(Op.getOperand(0));
  for (unsigned I = 2; I < NumOps; ++I)
    Ops.push_back(Op.getOperand(I));

  assert(Op->getNumValues() == 2 && "Expected only CC result and chain");
  SDVTList RawVTs = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue Intr = DAG.getNode(Opcode, SDLoc(Op), RawVTs, Ops);
  SDValue OldChain = SDValue(Op.getNode(), 1);
  SDValue NewChain = SDValue(Intr.getNode(), 0);
  DAG.ReplaceAllUsesOfValueWith(OldChain, NewChain);
  return Intr;
}

// Emit an intrinsic with a glued value instead of its CC result.
static SDValue emitIntrinsicWithGlue(SelectionDAG &DAG, SDValue Op,
                                     unsigned Opcode) {
  // Copy all operands except the intrinsic ID.
  unsigned NumOps = Op.getNumOperands();
  SmallVector<SDValue, 6> Ops;
  Ops.reserve(NumOps - 1);
  for (unsigned I = 1; I < NumOps; ++I)
    Ops.push_back(Op.getOperand(I));

  if (Op->getNumValues() == 1)
    return DAG.getNode(Opcode, SDLoc(Op), MVT::Glue, Ops);
  assert(Op->getNumValues() == 2 && "Expected exactly one non-CC result");
  SDVTList RawVTs = DAG.getVTList(Op->getValueType(0), MVT::Glue);
  return DAG.getNode(Opcode, SDLoc(Op), RawVTs, Ops);
}

// CC is a comparison that will be implemented using an integer or
// floating-point comparison.  Return the condition code mask for
// a branch on true.  In the integer case, CCMASK_CMP_UO is set for
// unsigned comparisons and clear for signed ones.  In the floating-point
// case, CCMASK_CMP_UO has its normal mask meaning (unordered).
static unsigned CCMaskForCondCode(ISD::CondCode CC) {
#define CONV(X) \
  case ISD::SET##X: return SystemZ::CCMASK_CMP_##X; \
  case ISD::SETO##X: return SystemZ::CCMASK_CMP_##X; \
  case ISD::SETU##X: return SystemZ::CCMASK_CMP_UO | SystemZ::CCMASK_CMP_##X

  switch (CC) {
  default:
    llvm_unreachable("Invalid integer condition!");

  CONV(EQ);
  CONV(NE);
  CONV(GT);
  CONV(GE);
  CONV(LT);
  CONV(LE);

  case ISD::SETO:  return SystemZ::CCMASK_CMP_O;
  case ISD::SETUO: return SystemZ::CCMASK_CMP_UO;
  }
#undef CONV
}

// Return a sequence for getting a 1 from an IPM result when CC has a
// value in CCMask and a 0 when CC has a value in CCValid & ~CCMask.
// The handling of CC values outside CCValid doesn't matter.
static IPMConversion getIPMConversion(unsigned CCValid, unsigned CCMask) {
  // Deal with cases where the result can be taken directly from a bit
  // of the IPM result.
  if (CCMask == (CCValid & (SystemZ::CCMASK_1 | SystemZ::CCMASK_3)))
    return IPMConversion(0, 0, SystemZ::IPM_CC);
  if (CCMask == (CCValid & (SystemZ::CCMASK_2 | SystemZ::CCMASK_3)))
    return IPMConversion(0, 0, SystemZ::IPM_CC + 1);

  // Deal with cases where we can add a value to force the sign bit
  // to contain the right value.  Putting the bit in 31 means we can
  // use SRL rather than RISBG(L), and also makes it easier to get a
  // 0/-1 value, so it has priority over the other tests below.
  //
  // These sequences rely on the fact that the upper two bits of the
  // IPM result are zero.
  uint64_t TopBit = uint64_t(1) << 31;
  if (CCMask == (CCValid & SystemZ::CCMASK_0))
    return IPMConversion(0, -(1 << SystemZ::IPM_CC), 31);
  if (CCMask == (CCValid & (SystemZ::CCMASK_0 | SystemZ::CCMASK_1)))
    return IPMConversion(0, -(2 << SystemZ::IPM_CC), 31);
  if (CCMask == (CCValid & (SystemZ::CCMASK_0
                            | SystemZ::CCMASK_1
                            | SystemZ::CCMASK_2)))
    return IPMConversion(0, -(3 << SystemZ::IPM_CC), 31);
  if (CCMask == (CCValid & SystemZ::CCMASK_3))
    return IPMConversion(0, TopBit - (3 << SystemZ::IPM_CC), 31);
  if (CCMask == (CCValid & (SystemZ::CCMASK_1
                            | SystemZ::CCMASK_2
                            | SystemZ::CCMASK_3)))
    return IPMConversion(0, TopBit - (1 << SystemZ::IPM_CC), 31);

  // Next try inverting the value and testing a bit.  0/1 could be
  // handled this way too, but we dealt with that case above.
  if (CCMask == (CCValid & (SystemZ::CCMASK_0 | SystemZ::CCMASK_2)))
    return IPMConversion(-1, 0, SystemZ::IPM_CC);

  // Handle cases where adding a value forces a non-sign bit to contain
  // the right value.
  if (CCMask == (CCValid & (SystemZ::CCMASK_1 | SystemZ::CCMASK_2)))
    return IPMConversion(0, 1 << SystemZ::IPM_CC, SystemZ::IPM_CC + 1);
  if (CCMask == (CCValid & (SystemZ::CCMASK_0 | SystemZ::CCMASK_3)))
    return IPMConversion(0, -(1 << SystemZ::IPM_CC), SystemZ::IPM_CC + 1);

  // The remaining cases are 1, 2, 0/1/3 and 0/2/3.  All these are
  // can be done by inverting the low CC bit and applying one of the
  // sign-based extractions above.
  if (CCMask == (CCValid & SystemZ::CCMASK_1))
    return IPMConversion(1 << SystemZ::IPM_CC, -(1 << SystemZ::IPM_CC), 31);
  if (CCMask == (CCValid & SystemZ::CCMASK_2))
    return IPMConversion(1 << SystemZ::IPM_CC,
                         TopBit - (3 << SystemZ::IPM_CC), 31);
  if (CCMask == (CCValid & (SystemZ::CCMASK_0
                            | SystemZ::CCMASK_1
                            | SystemZ::CCMASK_3)))
    return IPMConversion(1 << SystemZ::IPM_CC, -(3 << SystemZ::IPM_CC), 31);
  if (CCMask == (CCValid & (SystemZ::CCMASK_0
                            | SystemZ::CCMASK_2
                            | SystemZ::CCMASK_3)))
    return IPMConversion(1 << SystemZ::IPM_CC,
                         TopBit - (1 << SystemZ::IPM_CC), 31);

  llvm_unreachable("Unexpected CC combination");
}

// If C can be converted to a comparison against zero, adjust the operands
// as necessary.
static void adjustZeroCmp(SelectionDAG &DAG, SDLoc DL, Comparison &C) {
  if (C.ICmpType == SystemZICMP::UnsignedOnly)
    return;

  auto *ConstOp1 = dyn_cast<ConstantSDNode>(C.Op1.getNode());
  if (!ConstOp1)
    return;

  int64_t Value = ConstOp1->getSExtValue();
  if ((Value == -1 && C.CCMask == SystemZ::CCMASK_CMP_GT) ||
      (Value == -1 && C.CCMask == SystemZ::CCMASK_CMP_LE) ||
      (Value == 1 && C.CCMask == SystemZ::CCMASK_CMP_LT) ||
      (Value == 1 && C.CCMask == SystemZ::CCMASK_CMP_GE)) {
    C.CCMask ^= SystemZ::CCMASK_CMP_EQ;
    C.Op1 = DAG.getConstant(0, DL, C.Op1.getValueType());
  }
}

// If a comparison described by C is suitable for CLI(Y), CHHSI or CLHHSI,
// adjust the operands as necessary.
static void adjustSubwordCmp(SelectionDAG &DAG, SDLoc DL, Comparison &C) {
  // For us to make any changes, it must a comparison between a single-use
  // load and a constant.
  if (!C.Op0.hasOneUse() ||
      C.Op0.getOpcode() != ISD::LOAD ||
      C.Op1.getOpcode() != ISD::Constant)
    return;

  // We must have an 8- or 16-bit load.
  auto *Load = cast<LoadSDNode>(C.Op0);
  unsigned NumBits = Load->getMemoryVT().getStoreSizeInBits();
  if (NumBits != 8 && NumBits != 16)
    return;

  // The load must be an extending one and the constant must be within the
  // range of the unextended value.
  auto *ConstOp1 = cast<ConstantSDNode>(C.Op1);
  uint64_t Value = ConstOp1->getZExtValue();
  uint64_t Mask = (1 << NumBits) - 1;
  if (Load->getExtensionType() == ISD::SEXTLOAD) {
    // Make sure that ConstOp1 is in range of C.Op0.
    int64_t SignedValue = ConstOp1->getSExtValue();
    if (uint64_t(SignedValue) + (uint64_t(1) << (NumBits - 1)) > Mask)
      return;
    if (C.ICmpType != SystemZICMP::SignedOnly) {
      // Unsigned comparison between two sign-extended values is equivalent
      // to unsigned comparison between two zero-extended values.
      Value &= Mask;
    } else if (NumBits == 8) {
      // Try to treat the comparison as unsigned, so that we can use CLI.
      // Adjust CCMask and Value as necessary.
      if (Value == 0 && C.CCMask == SystemZ::CCMASK_CMP_LT)
        // Test whether the high bit of the byte is set.
        Value = 127, C.CCMask = SystemZ::CCMASK_CMP_GT;
      else if (Value == 0 && C.CCMask == SystemZ::CCMASK_CMP_GE)
        // Test whether the high bit of the byte is clear.
        Value = 128, C.CCMask = SystemZ::CCMASK_CMP_LT;
      else
        // No instruction exists for this combination.
        return;
      C.ICmpType = SystemZICMP::UnsignedOnly;
    }
  } else if (Load->getExtensionType() == ISD::ZEXTLOAD) {
    if (Value > Mask)
      return;
    // If the constant is in range, we can use any comparison.
    C.ICmpType = SystemZICMP::Any;
  } else
    return;

  // Make sure that the first operand is an i32 of the right extension type.
  ISD::LoadExtType ExtType = (C.ICmpType == SystemZICMP::SignedOnly ?
                              ISD::SEXTLOAD :
                              ISD::ZEXTLOAD);
  if (C.Op0.getValueType() != MVT::i32 ||
      Load->getExtensionType() != ExtType)
    C.Op0 = DAG.getExtLoad(ExtType, SDLoc(Load), MVT::i32,
                           Load->getChain(), Load->getBasePtr(),
                           Load->getPointerInfo(), Load->getMemoryVT(),
                           Load->isVolatile(), Load->isNonTemporal(),
                           Load->isInvariant(), Load->getAlignment());

  // Make sure that the second operand is an i32 with the right value.
  if (C.Op1.getValueType() != MVT::i32 ||
      Value != ConstOp1->getZExtValue())
    C.Op1 = DAG.getConstant(Value, DL, MVT::i32);
}

// Return true if Op is either an unextended load, or a load suitable
// for integer register-memory comparisons of type ICmpType.
static bool isNaturalMemoryOperand(SDValue Op, unsigned ICmpType) {
  auto *Load = dyn_cast<LoadSDNode>(Op.getNode());
  if (Load) {
    // There are no instructions to compare a register with a memory byte.
    if (Load->getMemoryVT() == MVT::i8)
      return false;
    // Otherwise decide on extension type.
    switch (Load->getExtensionType()) {
    case ISD::NON_EXTLOAD:
      return true;
    case ISD::SEXTLOAD:
      return ICmpType != SystemZICMP::UnsignedOnly;
    case ISD::ZEXTLOAD:
      return ICmpType != SystemZICMP::SignedOnly;
    default:
      break;
    }
  }
  return false;
}

// Return true if it is better to swap the operands of C.
static bool shouldSwapCmpOperands(const Comparison &C) {
  // Leave f128 comparisons alone, since they have no memory forms.
  if (C.Op0.getValueType() == MVT::f128)
    return false;

  // Always keep a floating-point constant second, since comparisons with
  // zero can use LOAD TEST and comparisons with other constants make a
  // natural memory operand.
  if (isa<ConstantFPSDNode>(C.Op1))
    return false;

  // Never swap comparisons with zero since there are many ways to optimize
  // those later.
  auto *ConstOp1 = dyn_cast<ConstantSDNode>(C.Op1);
  if (ConstOp1 && ConstOp1->getZExtValue() == 0)
    return false;

  // Also keep natural memory operands second if the loaded value is
  // only used here.  Several comparisons have memory forms.
  if (isNaturalMemoryOperand(C.Op1, C.ICmpType) && C.Op1.hasOneUse())
    return false;

  // Look for cases where Cmp0 is a single-use load and Cmp1 isn't.
  // In that case we generally prefer the memory to be second.
  if (isNaturalMemoryOperand(C.Op0, C.ICmpType) && C.Op0.hasOneUse()) {
    // The only exceptions are when the second operand is a constant and
    // we can use things like CHHSI.
    if (!ConstOp1)
      return true;
    // The unsigned memory-immediate instructions can handle 16-bit
    // unsigned integers.
    if (C.ICmpType != SystemZICMP::SignedOnly &&
        isUInt<16>(ConstOp1->getZExtValue()))
      return false;
    // The signed memory-immediate instructions can handle 16-bit
    // signed integers.
    if (C.ICmpType != SystemZICMP::UnsignedOnly &&
        isInt<16>(ConstOp1->getSExtValue()))
      return false;
    return true;
  }

  // Try to promote the use of CGFR and CLGFR.
  unsigned Opcode0 = C.Op0.getOpcode();
  if (C.ICmpType != SystemZICMP::UnsignedOnly && Opcode0 == ISD::SIGN_EXTEND)
    return true;
  if (C.ICmpType != SystemZICMP::SignedOnly && Opcode0 == ISD::ZERO_EXTEND)
    return true;
  if (C.ICmpType != SystemZICMP::SignedOnly &&
      Opcode0 == ISD::AND &&
      C.Op0.getOperand(1).getOpcode() == ISD::Constant &&
      cast<ConstantSDNode>(C.Op0.getOperand(1))->getZExtValue() == 0xffffffff)
    return true;

  return false;
}

// Return a version of comparison CC mask CCMask in which the LT and GT
// actions are swapped.
static unsigned reverseCCMask(unsigned CCMask) {
  return ((CCMask & SystemZ::CCMASK_CMP_EQ) |
          (CCMask & SystemZ::CCMASK_CMP_GT ? SystemZ::CCMASK_CMP_LT : 0) |
          (CCMask & SystemZ::CCMASK_CMP_LT ? SystemZ::CCMASK_CMP_GT : 0) |
          (CCMask & SystemZ::CCMASK_CMP_UO));
}

// Check whether C tests for equality between X and Y and whether X - Y
// or Y - X is also computed.  In that case it's better to compare the
// result of the subtraction against zero.
static void adjustForSubtraction(SelectionDAG &DAG, SDLoc DL, Comparison &C) {
  if (C.CCMask == SystemZ::CCMASK_CMP_EQ ||
      C.CCMask == SystemZ::CCMASK_CMP_NE) {
    for (auto I = C.Op0->use_begin(), E = C.Op0->use_end(); I != E; ++I) {
      SDNode *N = *I;
      if (N->getOpcode() == ISD::SUB &&
          ((N->getOperand(0) == C.Op0 && N->getOperand(1) == C.Op1) ||
           (N->getOperand(0) == C.Op1 && N->getOperand(1) == C.Op0))) {
        C.Op0 = SDValue(N, 0);
        C.Op1 = DAG.getConstant(0, DL, N->getValueType(0));
        return;
      }
    }
  }
}

// Check whether C compares a floating-point value with zero and if that
// floating-point value is also negated.  In this case we can use the
// negation to set CC, so avoiding separate LOAD AND TEST and
// LOAD (NEGATIVE/COMPLEMENT) instructions.
static void adjustForFNeg(Comparison &C) {
  auto *C1 = dyn_cast<ConstantFPSDNode>(C.Op1);
  if (C1 && C1->isZero()) {
    for (auto I = C.Op0->use_begin(), E = C.Op0->use_end(); I != E; ++I) {
      SDNode *N = *I;
      if (N->getOpcode() == ISD::FNEG) {
        C.Op0 = SDValue(N, 0);
        C.CCMask = reverseCCMask(C.CCMask);
        return;
      }
    }
  }
}

// Check whether C compares (shl X, 32) with 0 and whether X is
// also sign-extended.  In that case it is better to test the result
// of the sign extension using LTGFR.
//
// This case is important because InstCombine transforms a comparison
// with (sext (trunc X)) into a comparison with (shl X, 32).
static void adjustForLTGFR(Comparison &C) {
  // Check for a comparison between (shl X, 32) and 0.
  if (C.Op0.getOpcode() == ISD::SHL &&
      C.Op0.getValueType() == MVT::i64 &&
      C.Op1.getOpcode() == ISD::Constant &&
      cast<ConstantSDNode>(C.Op1)->getZExtValue() == 0) {
    auto *C1 = dyn_cast<ConstantSDNode>(C.Op0.getOperand(1));
    if (C1 && C1->getZExtValue() == 32) {
      SDValue ShlOp0 = C.Op0.getOperand(0);
      // See whether X has any SIGN_EXTEND_INREG uses.
      for (auto I = ShlOp0->use_begin(), E = ShlOp0->use_end(); I != E; ++I) {
        SDNode *N = *I;
        if (N->getOpcode() == ISD::SIGN_EXTEND_INREG &&
            cast<VTSDNode>(N->getOperand(1))->getVT() == MVT::i32) {
          C.Op0 = SDValue(N, 0);
          return;
        }
      }
    }
  }
}

// If C compares the truncation of an extending load, try to compare
// the untruncated value instead.  This exposes more opportunities to
// reuse CC.
static void adjustICmpTruncate(SelectionDAG &DAG, SDLoc DL, Comparison &C) {
  if (C.Op0.getOpcode() == ISD::TRUNCATE &&
      C.Op0.getOperand(0).getOpcode() == ISD::LOAD &&
      C.Op1.getOpcode() == ISD::Constant &&
      cast<ConstantSDNode>(C.Op1)->getZExtValue() == 0) {
    auto *L = cast<LoadSDNode>(C.Op0.getOperand(0));
    if (L->getMemoryVT().getStoreSizeInBits()
        <= C.Op0.getValueType().getSizeInBits()) {
      unsigned Type = L->getExtensionType();
      if ((Type == ISD::ZEXTLOAD && C.ICmpType != SystemZICMP::SignedOnly) ||
          (Type == ISD::SEXTLOAD && C.ICmpType != SystemZICMP::UnsignedOnly)) {
        C.Op0 = C.Op0.getOperand(0);
        C.Op1 = DAG.getConstant(0, DL, C.Op0.getValueType());
      }
    }
  }
}

// Return true if shift operation N has an in-range constant shift value.
// Store it in ShiftVal if so.
static bool isSimpleShift(SDValue N, unsigned &ShiftVal) {
  auto *Shift = dyn_cast<ConstantSDNode>(N.getOperand(1));
  if (!Shift)
    return false;

  uint64_t Amount = Shift->getZExtValue();
  if (Amount >= N.getValueType().getSizeInBits())
    return false;

  ShiftVal = Amount;
  return true;
}

// Check whether an AND with Mask is suitable for a TEST UNDER MASK
// instruction and whether the CC value is descriptive enough to handle
// a comparison of type Opcode between the AND result and CmpVal.
// CCMask says which comparison result is being tested and BitSize is
// the number of bits in the operands.  If TEST UNDER MASK can be used,
// return the corresponding CC mask, otherwise return 0.
static unsigned getTestUnderMaskCond(unsigned BitSize, unsigned CCMask,
                                     uint64_t Mask, uint64_t CmpVal,
                                     unsigned ICmpType) {
  assert(Mask != 0 && "ANDs with zero should have been removed by now");

  // Check whether the mask is suitable for TMHH, TMHL, TMLH or TMLL.
  if (!SystemZ::isImmLL(Mask) && !SystemZ::isImmLH(Mask) &&
      !SystemZ::isImmHL(Mask) && !SystemZ::isImmHH(Mask))
    return 0;

  // Work out the masks for the lowest and highest bits.
  unsigned HighShift = 63 - countLeadingZeros(Mask);
  uint64_t High = uint64_t(1) << HighShift;
  uint64_t Low = uint64_t(1) << countTrailingZeros(Mask);

  // Signed ordered comparisons are effectively unsigned if the sign
  // bit is dropped.
  bool EffectivelyUnsigned = (ICmpType != SystemZICMP::SignedOnly);

  // Check for equality comparisons with 0, or the equivalent.
  if (CmpVal == 0) {
    if (CCMask == SystemZ::CCMASK_CMP_EQ)
      return SystemZ::CCMASK_TM_ALL_0;
    if (CCMask == SystemZ::CCMASK_CMP_NE)
      return SystemZ::CCMASK_TM_SOME_1;
  }
  if (EffectivelyUnsigned && CmpVal <= Low) {
    if (CCMask == SystemZ::CCMASK_CMP_LT)
      return SystemZ::CCMASK_TM_ALL_0;
    if (CCMask == SystemZ::CCMASK_CMP_GE)
      return SystemZ::CCMASK_TM_SOME_1;
  }
  if (EffectivelyUnsigned && CmpVal < Low) {
    if (CCMask == SystemZ::CCMASK_CMP_LE)
      return SystemZ::CCMASK_TM_ALL_0;
    if (CCMask == SystemZ::CCMASK_CMP_GT)
      return SystemZ::CCMASK_TM_SOME_1;
  }

  // Check for equality comparisons with the mask, or the equivalent.
  if (CmpVal == Mask) {
    if (CCMask == SystemZ::CCMASK_CMP_EQ)
      return SystemZ::CCMASK_TM_ALL_1;
    if (CCMask == SystemZ::CCMASK_CMP_NE)
      return SystemZ::CCMASK_TM_SOME_0;
  }
  if (EffectivelyUnsigned && CmpVal >= Mask - Low && CmpVal < Mask) {
    if (CCMask == SystemZ::CCMASK_CMP_GT)
      return SystemZ::CCMASK_TM_ALL_1;
    if (CCMask == SystemZ::CCMASK_CMP_LE)
      return SystemZ::CCMASK_TM_SOME_0;
  }
  if (EffectivelyUnsigned && CmpVal > Mask - Low && CmpVal <= Mask) {
    if (CCMask == SystemZ::CCMASK_CMP_GE)
      return SystemZ::CCMASK_TM_ALL_1;
    if (CCMask == SystemZ::CCMASK_CMP_LT)
      return SystemZ::CCMASK_TM_SOME_0;
  }

  // Check for ordered comparisons with the top bit.
  if (EffectivelyUnsigned && CmpVal >= Mask - High && CmpVal < High) {
    if (CCMask == SystemZ::CCMASK_CMP_LE)
      return SystemZ::CCMASK_TM_MSB_0;
    if (CCMask == SystemZ::CCMASK_CMP_GT)
      return SystemZ::CCMASK_TM_MSB_1;
  }
  if (EffectivelyUnsigned && CmpVal > Mask - High && CmpVal <= High) {
    if (CCMask == SystemZ::CCMASK_CMP_LT)
      return SystemZ::CCMASK_TM_MSB_0;
    if (CCMask == SystemZ::CCMASK_CMP_GE)
      return SystemZ::CCMASK_TM_MSB_1;
  }

  // If there are just two bits, we can do equality checks for Low and High
  // as well.
  if (Mask == Low + High) {
    if (CCMask == SystemZ::CCMASK_CMP_EQ && CmpVal == Low)
      return SystemZ::CCMASK_TM_MIXED_MSB_0;
    if (CCMask == SystemZ::CCMASK_CMP_NE && CmpVal == Low)
      return SystemZ::CCMASK_TM_MIXED_MSB_0 ^ SystemZ::CCMASK_ANY;
    if (CCMask == SystemZ::CCMASK_CMP_EQ && CmpVal == High)
      return SystemZ::CCMASK_TM_MIXED_MSB_1;
    if (CCMask == SystemZ::CCMASK_CMP_NE && CmpVal == High)
      return SystemZ::CCMASK_TM_MIXED_MSB_1 ^ SystemZ::CCMASK_ANY;
  }

  // Looks like we've exhausted our options.
  return 0;
}

// See whether C can be implemented as a TEST UNDER MASK instruction.
// Update the arguments with the TM version if so.
static void adjustForTestUnderMask(SelectionDAG &DAG, SDLoc DL, Comparison &C) {
  // Check that we have a comparison with a constant.
  auto *ConstOp1 = dyn_cast<ConstantSDNode>(C.Op1);
  if (!ConstOp1)
    return;
  uint64_t CmpVal = ConstOp1->getZExtValue();

  // Check whether the nonconstant input is an AND with a constant mask.
  Comparison NewC(C);
  uint64_t MaskVal;
  ConstantSDNode *Mask = nullptr;
  if (C.Op0.getOpcode() == ISD::AND) {
    NewC.Op0 = C.Op0.getOperand(0);
    NewC.Op1 = C.Op0.getOperand(1);
    Mask = dyn_cast<ConstantSDNode>(NewC.Op1);
    if (!Mask)
      return;
    MaskVal = Mask->getZExtValue();
  } else {
    // There is no instruction to compare with a 64-bit immediate
    // so use TMHH instead if possible.  We need an unsigned ordered
    // comparison with an i64 immediate.
    if (NewC.Op0.getValueType() != MVT::i64 ||
        NewC.CCMask == SystemZ::CCMASK_CMP_EQ ||
        NewC.CCMask == SystemZ::CCMASK_CMP_NE ||
        NewC.ICmpType == SystemZICMP::SignedOnly)
      return;
    // Convert LE and GT comparisons into LT and GE.
    if (NewC.CCMask == SystemZ::CCMASK_CMP_LE ||
        NewC.CCMask == SystemZ::CCMASK_CMP_GT) {
      if (CmpVal == uint64_t(-1))
        return;
      CmpVal += 1;
      NewC.CCMask ^= SystemZ::CCMASK_CMP_EQ;
    }
    // If the low N bits of Op1 are zero than the low N bits of Op0 can
    // be masked off without changing the result.
    MaskVal = -(CmpVal & -CmpVal);
    NewC.ICmpType = SystemZICMP::UnsignedOnly;
  }
  if (!MaskVal)
    return;

  // Check whether the combination of mask, comparison value and comparison
  // type are suitable.
  unsigned BitSize = NewC.Op0.getValueType().getSizeInBits();
  unsigned NewCCMask, ShiftVal;
  if (NewC.ICmpType != SystemZICMP::SignedOnly &&
      NewC.Op0.getOpcode() == ISD::SHL &&
      isSimpleShift(NewC.Op0, ShiftVal) &&
      (NewCCMask = getTestUnderMaskCond(BitSize, NewC.CCMask,
                                        MaskVal >> ShiftVal,
                                        CmpVal >> ShiftVal,
                                        SystemZICMP::Any))) {
    NewC.Op0 = NewC.Op0.getOperand(0);
    MaskVal >>= ShiftVal;
  } else if (NewC.ICmpType != SystemZICMP::SignedOnly &&
             NewC.Op0.getOpcode() == ISD::SRL &&
             isSimpleShift(NewC.Op0, ShiftVal) &&
             (NewCCMask = getTestUnderMaskCond(BitSize, NewC.CCMask,
                                               MaskVal << ShiftVal,
                                               CmpVal << ShiftVal,
                                               SystemZICMP::UnsignedOnly))) {
    NewC.Op0 = NewC.Op0.getOperand(0);
    MaskVal <<= ShiftVal;
  } else {
    NewCCMask = getTestUnderMaskCond(BitSize, NewC.CCMask, MaskVal, CmpVal,
                                     NewC.ICmpType);
    if (!NewCCMask)
      return;
  }

  // Go ahead and make the change.
  C.Opcode = SystemZISD::TM;
  C.Op0 = NewC.Op0;
  if (Mask && Mask->getZExtValue() == MaskVal)
    C.Op1 = SDValue(Mask, 0);
  else
    C.Op1 = DAG.getConstant(MaskVal, DL, C.Op0.getValueType());
  C.CCValid = SystemZ::CCMASK_TM;
  C.CCMask = NewCCMask;
}

// Return a Comparison that tests the condition-code result of intrinsic
// node Call against constant integer CC using comparison code Cond.
// Opcode is the opcode of the SystemZISD operation for the intrinsic
// and CCValid is the set of possible condition-code results.
static Comparison getIntrinsicCmp(SelectionDAG &DAG, unsigned Opcode,
                                  SDValue Call, unsigned CCValid, uint64_t CC,
                                  ISD::CondCode Cond) {
  Comparison C(Call, SDValue());
  C.Opcode = Opcode;
  C.CCValid = CCValid;
  if (Cond == ISD::SETEQ)
    // bit 3 for CC==0, bit 0 for CC==3, always false for CC>3.
    C.CCMask = CC < 4 ? 1 << (3 - CC) : 0;
  else if (Cond == ISD::SETNE)
    // ...and the inverse of that.
    C.CCMask = CC < 4 ? ~(1 << (3 - CC)) : -1;
  else if (Cond == ISD::SETLT || Cond == ISD::SETULT)
    // bits above bit 3 for CC==0 (always false), bits above bit 0 for CC==3,
    // always true for CC>3.
    C.CCMask = CC < 4 ? ~0U << (4 - CC) : -1;
  else if (Cond == ISD::SETGE || Cond == ISD::SETUGE)
    // ...and the inverse of that.
    C.CCMask = CC < 4 ? ~(~0U << (4 - CC)) : 0;
  else if (Cond == ISD::SETLE || Cond == ISD::SETULE)
    // bit 3 and above for CC==0, bit 0 and above for CC==3 (always true),
    // always true for CC>3.
    C.CCMask = CC < 4 ? ~0U << (3 - CC) : -1;
  else if (Cond == ISD::SETGT || Cond == ISD::SETUGT)
    // ...and the inverse of that.
    C.CCMask = CC < 4 ? ~(~0U << (3 - CC)) : 0;
  else
    llvm_unreachable("Unexpected integer comparison type");
  C.CCMask &= CCValid;
  return C;
}

// Decide how to implement a comparison of type Cond between CmpOp0 with CmpOp1.
static Comparison getCmp(SelectionDAG &DAG, SDValue CmpOp0, SDValue CmpOp1,
                         ISD::CondCode Cond, SDLoc DL) {
  if (CmpOp1.getOpcode() == ISD::Constant) {
    uint64_t Constant = cast<ConstantSDNode>(CmpOp1)->getZExtValue();
    unsigned Opcode, CCValid;
    if (CmpOp0.getOpcode() == ISD::INTRINSIC_W_CHAIN &&
        CmpOp0.getResNo() == 0 && CmpOp0->hasNUsesOfValue(1, 0) &&
        isIntrinsicWithCCAndChain(CmpOp0, Opcode, CCValid))
      return getIntrinsicCmp(DAG, Opcode, CmpOp0, CCValid, Constant, Cond);
    if (CmpOp0.getOpcode() == ISD::INTRINSIC_WO_CHAIN &&
        CmpOp0.getResNo() == CmpOp0->getNumValues() - 1 &&
        isIntrinsicWithCC(CmpOp0, Opcode, CCValid))
      return getIntrinsicCmp(DAG, Opcode, CmpOp0, CCValid, Constant, Cond);
  }
  Comparison C(CmpOp0, CmpOp1);
  C.CCMask = CCMaskForCondCode(Cond);
  if (C.Op0.getValueType().isFloatingPoint()) {
    C.CCValid = SystemZ::CCMASK_FCMP;
    C.Opcode = SystemZISD::FCMP;
    adjustForFNeg(C);
  } else {
    C.CCValid = SystemZ::CCMASK_ICMP;
    C.Opcode = SystemZISD::ICMP;
    // Choose the type of comparison.  Equality and inequality tests can
    // use either signed or unsigned comparisons.  The choice also doesn't
    // matter if both sign bits are known to be clear.  In those cases we
    // want to give the main isel code the freedom to choose whichever
    // form fits best.
    if (C.CCMask == SystemZ::CCMASK_CMP_EQ ||
        C.CCMask == SystemZ::CCMASK_CMP_NE ||
        (DAG.SignBitIsZero(C.Op0) && DAG.SignBitIsZero(C.Op1)))
      C.ICmpType = SystemZICMP::Any;
    else if (C.CCMask & SystemZ::CCMASK_CMP_UO)
      C.ICmpType = SystemZICMP::UnsignedOnly;
    else
      C.ICmpType = SystemZICMP::SignedOnly;
    C.CCMask &= ~SystemZ::CCMASK_CMP_UO;
    adjustZeroCmp(DAG, DL, C);
    adjustSubwordCmp(DAG, DL, C);
    adjustForSubtraction(DAG, DL, C);
    adjustForLTGFR(C);
    adjustICmpTruncate(DAG, DL, C);
  }

  if (shouldSwapCmpOperands(C)) {
    std::swap(C.Op0, C.Op1);
    C.CCMask = reverseCCMask(C.CCMask);
  }

  adjustForTestUnderMask(DAG, DL, C);
  return C;
}

// Emit the comparison instruction described by C.
static SDValue emitCmp(SelectionDAG &DAG, SDLoc DL, Comparison &C) {
  if (!C.Op1.getNode()) {
    SDValue Op;
    switch (C.Op0.getOpcode()) {
    case ISD::INTRINSIC_W_CHAIN:
      Op = emitIntrinsicWithChainAndGlue(DAG, C.Op0, C.Opcode);
      break;
    case ISD::INTRINSIC_WO_CHAIN:
      Op = emitIntrinsicWithGlue(DAG, C.Op0, C.Opcode);
      break;
    default:
      llvm_unreachable("Invalid comparison operands");
    }
    return SDValue(Op.getNode(), Op->getNumValues() - 1);
  }
  if (C.Opcode == SystemZISD::ICMP)
    return DAG.getNode(SystemZISD::ICMP, DL, MVT::Glue, C.Op0, C.Op1,
                       DAG.getConstant(C.ICmpType, DL, MVT::i32));
  if (C.Opcode == SystemZISD::TM) {
    bool RegisterOnly = (bool(C.CCMask & SystemZ::CCMASK_TM_MIXED_MSB_0) !=
                         bool(C.CCMask & SystemZ::CCMASK_TM_MIXED_MSB_1));
    return DAG.getNode(SystemZISD::TM, DL, MVT::Glue, C.Op0, C.Op1,
                       DAG.getConstant(RegisterOnly, DL, MVT::i32));
  }
  return DAG.getNode(C.Opcode, DL, MVT::Glue, C.Op0, C.Op1);
}

// Implement a 32-bit *MUL_LOHI operation by extending both operands to
// 64 bits.  Extend is the extension type to use.  Store the high part
// in Hi and the low part in Lo.
static void lowerMUL_LOHI32(SelectionDAG &DAG, SDLoc DL,
                            unsigned Extend, SDValue Op0, SDValue Op1,
                            SDValue &Hi, SDValue &Lo) {
  Op0 = DAG.getNode(Extend, DL, MVT::i64, Op0);
  Op1 = DAG.getNode(Extend, DL, MVT::i64, Op1);
  SDValue Mul = DAG.getNode(ISD::MUL, DL, MVT::i64, Op0, Op1);
  Hi = DAG.getNode(ISD::SRL, DL, MVT::i64, Mul,
                   DAG.getConstant(32, DL, MVT::i64));
  Hi = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Hi);
  Lo = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Mul);
}

// Lower a binary operation that produces two VT results, one in each
// half of a GR128 pair.  Op0 and Op1 are the VT operands to the operation,
// Extend extends Op0 to a GR128, and Opcode performs the GR128 operation
// on the extended Op0 and (unextended) Op1.  Store the even register result
// in Even and the odd register result in Odd.
static void lowerGR128Binary(SelectionDAG &DAG, SDLoc DL, EVT VT,
                             unsigned Extend, unsigned Opcode,
                             SDValue Op0, SDValue Op1,
                             SDValue &Even, SDValue &Odd) {
  SDNode *In128 = DAG.getMachineNode(Extend, DL, MVT::Untyped, Op0);
  SDValue Result = DAG.getNode(Opcode, DL, MVT::Untyped,
                               SDValue(In128, 0), Op1);
  bool Is32Bit = is32Bit(VT);
  Even = DAG.getTargetExtractSubreg(SystemZ::even128(Is32Bit), DL, VT, Result);
  Odd = DAG.getTargetExtractSubreg(SystemZ::odd128(Is32Bit), DL, VT, Result);
}

// Return an i32 value that is 1 if the CC value produced by Glue is
// in the mask CCMask and 0 otherwise.  CC is known to have a value
// in CCValid, so other values can be ignored.
static SDValue emitSETCC(SelectionDAG &DAG, SDLoc DL, SDValue Glue,
                         unsigned CCValid, unsigned CCMask) {
  IPMConversion Conversion = getIPMConversion(CCValid, CCMask);
  SDValue Result = DAG.getNode(SystemZISD::IPM, DL, MVT::i32, Glue);

  if (Conversion.XORValue)
    Result = DAG.getNode(ISD::XOR, DL, MVT::i32, Result,
                         DAG.getConstant(Conversion.XORValue, DL, MVT::i32));

  if (Conversion.AddValue)
    Result = DAG.getNode(ISD::ADD, DL, MVT::i32, Result,
                         DAG.getConstant(Conversion.AddValue, DL, MVT::i32));

  // The SHR/AND sequence should get optimized to an RISBG.
  Result = DAG.getNode(ISD::SRL, DL, MVT::i32, Result,
                       DAG.getConstant(Conversion.Bit, DL, MVT::i32));
  if (Conversion.Bit != 31)
    Result = DAG.getNode(ISD::AND, DL, MVT::i32, Result,
                         DAG.getConstant(1, DL, MVT::i32));
  return Result;
}

// Return the SystemISD vector comparison operation for CC, or 0 if it cannot
// be done directly.  IsFP is true if CC is for a floating-point rather than
// integer comparison.
static unsigned getVectorComparison(ISD::CondCode CC, bool IsFP) {
  switch (CC) {
  case ISD::SETOEQ:
  case ISD::SETEQ:
    return IsFP ? SystemZISD::VFCMPE : SystemZISD::VICMPE;

  case ISD::SETOGE:
  case ISD::SETGE:
    return IsFP ? SystemZISD::VFCMPHE : static_cast<SystemZISD::NodeType>(0);

  case ISD::SETOGT:
  case ISD::SETGT:
    return IsFP ? SystemZISD::VFCMPH : SystemZISD::VICMPH;

  case ISD::SETUGT:
    return IsFP ? static_cast<SystemZISD::NodeType>(0) : SystemZISD::VICMPHL;

  default:
    return 0;
  }
}

// Return the SystemZISD vector comparison operation for CC or its inverse,
// or 0 if neither can be done directly.  Indicate in Invert whether the
// result is for the inverse of CC.  IsFP is true if CC is for a
// floating-point rather than integer comparison.
static unsigned getVectorComparisonOrInvert(ISD::CondCode CC, bool IsFP,
                                            bool &Invert) {
  if (unsigned Opcode = getVectorComparison(CC, IsFP)) {
    Invert = false;
    return Opcode;
  }

  CC = ISD::getSetCCInverse(CC, !IsFP);
  if (unsigned Opcode = getVectorComparison(CC, IsFP)) {
    Invert = true;
    return Opcode;
  }

  return 0;
}

// Return a v2f64 that contains the extended form of elements Start and Start+1
// of v4f32 value Op.
static SDValue expandV4F32ToV2F64(SelectionDAG &DAG, int Start, SDLoc DL,
                                  SDValue Op) {
  int Mask[] = { Start, -1, Start + 1, -1 };
  Op = DAG.getVectorShuffle(MVT::v4f32, DL, Op, DAG.getUNDEF(MVT::v4f32), Mask);
  return DAG.getNode(SystemZISD::VEXTEND, DL, MVT::v2f64, Op);
}

// Build a comparison of vectors CmpOp0 and CmpOp1 using opcode Opcode,
// producing a result of type VT.
static SDValue getVectorCmp(SelectionDAG &DAG, unsigned Opcode, SDLoc DL,
                            EVT VT, SDValue CmpOp0, SDValue CmpOp1) {
  // There is no hardware support for v4f32, so extend the vector into
  // two v2f64s and compare those.
  if (CmpOp0.getValueType() == MVT::v4f32) {
    SDValue H0 = expandV4F32ToV2F64(DAG, 0, DL, CmpOp0);
    SDValue L0 = expandV4F32ToV2F64(DAG, 2, DL, CmpOp0);
    SDValue H1 = expandV4F32ToV2F64(DAG, 0, DL, CmpOp1);
    SDValue L1 = expandV4F32ToV2F64(DAG, 2, DL, CmpOp1);
    SDValue HRes = DAG.getNode(Opcode, DL, MVT::v2i64, H0, H1);
    SDValue LRes = DAG.getNode(Opcode, DL, MVT::v2i64, L0, L1);
    return DAG.getNode(SystemZISD::PACK, DL, VT, HRes, LRes);
  }
  return DAG.getNode(Opcode, DL, VT, CmpOp0, CmpOp1);
}

// Lower a vector comparison of type CC between CmpOp0 and CmpOp1, producing
// an integer mask of type VT.
static SDValue lowerVectorSETCC(SelectionDAG &DAG, SDLoc DL, EVT VT,
                                ISD::CondCode CC, SDValue CmpOp0,
                                SDValue CmpOp1) {
  bool IsFP = CmpOp0.getValueType().isFloatingPoint();
  bool Invert = false;
  SDValue Cmp;
  switch (CC) {
    // Handle tests for order using (or (ogt y x) (oge x y)).
  case ISD::SETUO:
    Invert = true;
  case ISD::SETO: {
    assert(IsFP && "Unexpected integer comparison");
    SDValue LT = getVectorCmp(DAG, SystemZISD::VFCMPH, DL, VT, CmpOp1, CmpOp0);
    SDValue GE = getVectorCmp(DAG, SystemZISD::VFCMPHE, DL, VT, CmpOp0, CmpOp1);
    Cmp = DAG.getNode(ISD::OR, DL, VT, LT, GE);
    break;
  }

    // Handle <> tests using (or (ogt y x) (ogt x y)).
  case ISD::SETUEQ:
    Invert = true;
  case ISD::SETONE: {
    assert(IsFP && "Unexpected integer comparison");
    SDValue LT = getVectorCmp(DAG, SystemZISD::VFCMPH, DL, VT, CmpOp1, CmpOp0);
    SDValue GT = getVectorCmp(DAG, SystemZISD::VFCMPH, DL, VT, CmpOp0, CmpOp1);
    Cmp = DAG.getNode(ISD::OR, DL, VT, LT, GT);
    break;
  }

    // Otherwise a single comparison is enough.  It doesn't really
    // matter whether we try the inversion or the swap first, since
    // there are no cases where both work.
  default:
    if (unsigned Opcode = getVectorComparisonOrInvert(CC, IsFP, Invert))
      Cmp = getVectorCmp(DAG, Opcode, DL, VT, CmpOp0, CmpOp1);
    else {
      CC = ISD::getSetCCSwappedOperands(CC);
      if (unsigned Opcode = getVectorComparisonOrInvert(CC, IsFP, Invert))
        Cmp = getVectorCmp(DAG, Opcode, DL, VT, CmpOp1, CmpOp0);
      else
        llvm_unreachable("Unhandled comparison");
    }
    break;
  }
  if (Invert) {
    SDValue Mask = DAG.getNode(SystemZISD::BYTE_MASK, DL, MVT::v16i8,
                               DAG.getConstant(65535, DL, MVT::i32));
    Mask = DAG.getNode(ISD::BITCAST, DL, VT, Mask);
    Cmp = DAG.getNode(ISD::XOR, DL, VT, Cmp, Mask);
  }
  return Cmp;
}

SDValue SystemZTargetLowering::lowerSETCC(SDValue Op,
                                          SelectionDAG &DAG) const {
  SDValue CmpOp0   = Op.getOperand(0);
  SDValue CmpOp1   = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  if (VT.isVector())
    return lowerVectorSETCC(DAG, DL, VT, CC, CmpOp0, CmpOp1);

  Comparison C(getCmp(DAG, CmpOp0, CmpOp1, CC, DL));
  SDValue Glue = emitCmp(DAG, DL, C);
  return emitSETCC(DAG, DL, Glue, C.CCValid, C.CCMask);
}

SDValue SystemZTargetLowering::lowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue CmpOp0   = Op.getOperand(2);
  SDValue CmpOp1   = Op.getOperand(3);
  SDValue Dest     = Op.getOperand(4);
  SDLoc DL(Op);

  Comparison C(getCmp(DAG, CmpOp0, CmpOp1, CC, DL));
  SDValue Glue = emitCmp(DAG, DL, C);
  return DAG.getNode(SystemZISD::BR_CCMASK, DL, Op.getValueType(),
                     Op.getOperand(0), DAG.getConstant(C.CCValid, DL, MVT::i32),
                     DAG.getConstant(C.CCMask, DL, MVT::i32), Dest, Glue);
}

// Return true if Pos is CmpOp and Neg is the negative of CmpOp,
// allowing Pos and Neg to be wider than CmpOp.
static bool isAbsolute(SDValue CmpOp, SDValue Pos, SDValue Neg) {
  return (Neg.getOpcode() == ISD::SUB &&
          Neg.getOperand(0).getOpcode() == ISD::Constant &&
          cast<ConstantSDNode>(Neg.getOperand(0))->getZExtValue() == 0 &&
          Neg.getOperand(1) == Pos &&
          (Pos == CmpOp ||
           (Pos.getOpcode() == ISD::SIGN_EXTEND &&
            Pos.getOperand(0) == CmpOp)));
}

// Return the absolute or negative absolute of Op; IsNegative decides which.
static SDValue getAbsolute(SelectionDAG &DAG, SDLoc DL, SDValue Op,
                           bool IsNegative) {
  Op = DAG.getNode(SystemZISD::IABS, DL, Op.getValueType(), Op);
  if (IsNegative)
    Op = DAG.getNode(ISD::SUB, DL, Op.getValueType(),
                     DAG.getConstant(0, DL, Op.getValueType()), Op);
  return Op;
}

SDValue SystemZTargetLowering::lowerSELECT_CC(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDValue CmpOp0   = Op.getOperand(0);
  SDValue CmpOp1   = Op.getOperand(1);
  SDValue TrueOp   = Op.getOperand(2);
  SDValue FalseOp  = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc DL(Op);

  Comparison C(getCmp(DAG, CmpOp0, CmpOp1, CC, DL));

  // Check for absolute and negative-absolute selections, including those
  // where the comparison value is sign-extended (for LPGFR and LNGFR).
  // This check supplements the one in DAGCombiner.
  if (C.Opcode == SystemZISD::ICMP &&
      C.CCMask != SystemZ::CCMASK_CMP_EQ &&
      C.CCMask != SystemZ::CCMASK_CMP_NE &&
      C.Op1.getOpcode() == ISD::Constant &&
      cast<ConstantSDNode>(C.Op1)->getZExtValue() == 0) {
    if (isAbsolute(C.Op0, TrueOp, FalseOp))
      return getAbsolute(DAG, DL, TrueOp, C.CCMask & SystemZ::CCMASK_CMP_LT);
    if (isAbsolute(C.Op0, FalseOp, TrueOp))
      return getAbsolute(DAG, DL, FalseOp, C.CCMask & SystemZ::CCMASK_CMP_GT);
  }

  SDValue Glue = emitCmp(DAG, DL, C);

  // Special case for handling -1/0 results.  The shifts we use here
  // should get optimized with the IPM conversion sequence.
  auto *TrueC = dyn_cast<ConstantSDNode>(TrueOp);
  auto *FalseC = dyn_cast<ConstantSDNode>(FalseOp);
  if (TrueC && FalseC) {
    int64_t TrueVal = TrueC->getSExtValue();
    int64_t FalseVal = FalseC->getSExtValue();
    if ((TrueVal == -1 && FalseVal == 0) || (TrueVal == 0 && FalseVal == -1)) {
      // Invert the condition if we want -1 on false.
      if (TrueVal == 0)
        C.CCMask ^= C.CCValid;
      SDValue Result = emitSETCC(DAG, DL, Glue, C.CCValid, C.CCMask);
      EVT VT = Op.getValueType();
      // Extend the result to VT.  Upper bits are ignored.
      if (!is32Bit(VT))
        Result = DAG.getNode(ISD::ANY_EXTEND, DL, VT, Result);
      // Sign-extend from the low bit.
      SDValue ShAmt = DAG.getConstant(VT.getSizeInBits() - 1, DL, MVT::i32);
      SDValue Shl = DAG.getNode(ISD::SHL, DL, VT, Result, ShAmt);
      return DAG.getNode(ISD::SRA, DL, VT, Shl, ShAmt);
    }
  }

  SDValue Ops[] = {TrueOp, FalseOp, DAG.getConstant(C.CCValid, DL, MVT::i32),
                   DAG.getConstant(C.CCMask, DL, MVT::i32), Glue};

  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::Glue);
  return DAG.getNode(SystemZISD::SELECT_CCMASK, DL, VTs, Ops);
}

SDValue SystemZTargetLowering::lowerGlobalAddress(GlobalAddressSDNode *Node,
                                                  SelectionDAG &DAG) const {
  SDLoc DL(Node);
  const GlobalValue *GV = Node->getGlobal();
  int64_t Offset = Node->getOffset();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  Reloc::Model RM = DAG.getTarget().getRelocationModel();
  CodeModel::Model CM = DAG.getTarget().getCodeModel();

  SDValue Result;
  if (Subtarget.isPC32DBLSymbol(GV, RM, CM)) {
    // Assign anchors at 1<<12 byte boundaries.
    uint64_t Anchor = Offset & ~uint64_t(0xfff);
    Result = DAG.getTargetGlobalAddress(GV, DL, PtrVT, Anchor);
    Result = DAG.getNode(SystemZISD::PCREL_WRAPPER, DL, PtrVT, Result);

    // The offset can be folded into the address if it is aligned to a halfword.
    Offset -= Anchor;
    if (Offset != 0 && (Offset & 1) == 0) {
      SDValue Full = DAG.getTargetGlobalAddress(GV, DL, PtrVT, Anchor + Offset);
      Result = DAG.getNode(SystemZISD::PCREL_OFFSET, DL, PtrVT, Full, Result);
      Offset = 0;
    }
  } else {
    Result = DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, SystemZII::MO_GOT);
    Result = DAG.getNode(SystemZISD::PCREL_WRAPPER, DL, PtrVT, Result);
    Result = DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), Result,
                         MachinePointerInfo::getGOT(DAG.getMachineFunction()),
                         false, false, false, 0);
  }

  // If there was a non-zero offset that we didn't fold, create an explicit
  // addition for it.
  if (Offset != 0)
    Result = DAG.getNode(ISD::ADD, DL, PtrVT, Result,
                         DAG.getConstant(Offset, DL, PtrVT));

  return Result;
}

SDValue SystemZTargetLowering::lowerTLSGetOffset(GlobalAddressSDNode *Node,
                                                 SelectionDAG &DAG,
                                                 unsigned Opcode,
                                                 SDValue GOTOffset) const {
  SDLoc DL(Node);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue Chain = DAG.getEntryNode();
  SDValue Glue;

  // __tls_get_offset takes the GOT offset in %r2 and the GOT in %r12.
  SDValue GOT = DAG.getGLOBAL_OFFSET_TABLE(PtrVT);
  Chain = DAG.getCopyToReg(Chain, DL, SystemZ::R12D, GOT, Glue);
  Glue = Chain.getValue(1);
  Chain = DAG.getCopyToReg(Chain, DL, SystemZ::R2D, GOTOffset, Glue);
  Glue = Chain.getValue(1);

  // The first call operand is the chain and the second is the TLS symbol.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(DAG.getTargetGlobalAddress(Node->getGlobal(), DL,
                                           Node->getValueType(0),
                                           0, 0));

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  Ops.push_back(DAG.getRegister(SystemZ::R2D, PtrVT));
  Ops.push_back(DAG.getRegister(SystemZ::R12D, PtrVT));

  // Add a register mask operand representing the call-preserved registers.
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const uint32_t *Mask =
      TRI->getCallPreservedMask(DAG.getMachineFunction(), CallingConv::C);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  // Glue the call to the argument copies.
  Ops.push_back(Glue);

  // Emit the call.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  Chain = DAG.getNode(Opcode, DL, NodeTys, Ops);
  Glue = Chain.getValue(1);

  // Copy the return value from %r2.
  return DAG.getCopyFromReg(Chain, DL, SystemZ::R2D, PtrVT, Glue);
}

SDValue SystemZTargetLowering::lowerGlobalTLSAddress(GlobalAddressSDNode *Node,
                                                     SelectionDAG &DAG) const {
  if (DAG.getTarget().Options.EmulatedTLS)
    return LowerToTLSEmulatedModel(Node, DAG);
  SDLoc DL(Node);
  const GlobalValue *GV = Node->getGlobal();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  TLSModel::Model model = DAG.getTarget().getTLSModel(GV);

  // The high part of the thread pointer is in access register 0.
  SDValue TPHi = DAG.getNode(SystemZISD::EXTRACT_ACCESS, DL, MVT::i32,
                             DAG.getConstant(0, DL, MVT::i32));
  TPHi = DAG.getNode(ISD::ANY_EXTEND, DL, PtrVT, TPHi);

  // The low part of the thread pointer is in access register 1.
  SDValue TPLo = DAG.getNode(SystemZISD::EXTRACT_ACCESS, DL, MVT::i32,
                             DAG.getConstant(1, DL, MVT::i32));
  TPLo = DAG.getNode(ISD::ZERO_EXTEND, DL, PtrVT, TPLo);

  // Merge them into a single 64-bit address.
  SDValue TPHiShifted = DAG.getNode(ISD::SHL, DL, PtrVT, TPHi,
                                    DAG.getConstant(32, DL, PtrVT));
  SDValue TP = DAG.getNode(ISD::OR, DL, PtrVT, TPHiShifted, TPLo);

  // Get the offset of GA from the thread pointer, based on the TLS model.
  SDValue Offset;
  switch (model) {
    case TLSModel::GeneralDynamic: {
      // Load the GOT offset of the tls_index (module ID / per-symbol offset).
      SystemZConstantPoolValue *CPV =
        SystemZConstantPoolValue::Create(GV, SystemZCP::TLSGD);

      Offset = DAG.getConstantPool(CPV, PtrVT, 8);
      Offset = DAG.getLoad(
          PtrVT, DL, DAG.getEntryNode(), Offset,
          MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), false,
          false, false, 0);

      // Call __tls_get_offset to retrieve the offset.
      Offset = lowerTLSGetOffset(Node, DAG, SystemZISD::TLS_GDCALL, Offset);
      break;
    }

    case TLSModel::LocalDynamic: {
      // Load the GOT offset of the module ID.
      SystemZConstantPoolValue *CPV =
        SystemZConstantPoolValue::Create(GV, SystemZCP::TLSLDM);

      Offset = DAG.getConstantPool(CPV, PtrVT, 8);
      Offset = DAG.getLoad(
          PtrVT, DL, DAG.getEntryNode(), Offset,
          MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), false,
          false, false, 0);

      // Call __tls_get_offset to retrieve the module base offset.
      Offset = lowerTLSGetOffset(Node, DAG, SystemZISD::TLS_LDCALL, Offset);

      // Note: The SystemZLDCleanupPass will remove redundant computations
      // of the module base offset.  Count total number of local-dynamic
      // accesses to trigger execution of that pass.
      SystemZMachineFunctionInfo* MFI =
        DAG.getMachineFunction().getInfo<SystemZMachineFunctionInfo>();
      MFI->incNumLocalDynamicTLSAccesses();

      // Add the per-symbol offset.
      CPV = SystemZConstantPoolValue::Create(GV, SystemZCP::DTPOFF);

      SDValue DTPOffset = DAG.getConstantPool(CPV, PtrVT, 8);
      DTPOffset = DAG.getLoad(
          PtrVT, DL, DAG.getEntryNode(), DTPOffset,
          MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), false,
          false, false, 0);

      Offset = DAG.getNode(ISD::ADD, DL, PtrVT, Offset, DTPOffset);
      break;
    }

    case TLSModel::InitialExec: {
      // Load the offset from the GOT.
      Offset = DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0,
                                          SystemZII::MO_INDNTPOFF);
      Offset = DAG.getNode(SystemZISD::PCREL_WRAPPER, DL, PtrVT, Offset);
      Offset = DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), Offset,
                           MachinePointerInfo::getGOT(DAG.getMachineFunction()),
                           false, false, false, 0);
      break;
    }

    case TLSModel::LocalExec: {
      // Force the offset into the constant pool and load it from there.
      SystemZConstantPoolValue *CPV =
        SystemZConstantPoolValue::Create(GV, SystemZCP::NTPOFF);

      Offset = DAG.getConstantPool(CPV, PtrVT, 8);
      Offset = DAG.getLoad(
          PtrVT, DL, DAG.getEntryNode(), Offset,
          MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), false,
          false, false, 0);
      break;
    }
  }

  // Add the base and offset together.
  return DAG.getNode(ISD::ADD, DL, PtrVT, TP, Offset);
}

SDValue SystemZTargetLowering::lowerBlockAddress(BlockAddressSDNode *Node,
                                                 SelectionDAG &DAG) const {
  SDLoc DL(Node);
  const BlockAddress *BA = Node->getBlockAddress();
  int64_t Offset = Node->getOffset();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  SDValue Result = DAG.getTargetBlockAddress(BA, PtrVT, Offset);
  Result = DAG.getNode(SystemZISD::PCREL_WRAPPER, DL, PtrVT, Result);
  return Result;
}

SDValue SystemZTargetLowering::lowerJumpTable(JumpTableSDNode *JT,
                                              SelectionDAG &DAG) const {
  SDLoc DL(JT);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue Result = DAG.getTargetJumpTable(JT->getIndex(), PtrVT);

  // Use LARL to load the address of the table.
  return DAG.getNode(SystemZISD::PCREL_WRAPPER, DL, PtrVT, Result);
}

SDValue SystemZTargetLowering::lowerConstantPool(ConstantPoolSDNode *CP,
                                                 SelectionDAG &DAG) const {
  SDLoc DL(CP);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  SDValue Result;
  if (CP->isMachineConstantPoolEntry())
    Result = DAG.getTargetConstantPool(CP->getMachineCPVal(), PtrVT,
                                       CP->getAlignment());
  else
    Result = DAG.getTargetConstantPool(CP->getConstVal(), PtrVT,
                                       CP->getAlignment(), CP->getOffset());

  // Use LARL to load the address of the constant pool entry.
  return DAG.getNode(SystemZISD::PCREL_WRAPPER, DL, PtrVT, Result);
}

SDValue SystemZTargetLowering::lowerBITCAST(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue In = Op.getOperand(0);
  EVT InVT = In.getValueType();
  EVT ResVT = Op.getValueType();

  // Convert loads directly.  This is normally done by DAGCombiner,
  // but we need this case for bitcasts that are created during lowering
  // and which are then lowered themselves.
  if (auto *LoadN = dyn_cast<LoadSDNode>(In))
    return DAG.getLoad(ResVT, DL, LoadN->getChain(), LoadN->getBasePtr(),
                       LoadN->getMemOperand());

  if (InVT == MVT::i32 && ResVT == MVT::f32) {
    SDValue In64;
    if (Subtarget.hasHighWord()) {
      SDNode *U64 = DAG.getMachineNode(TargetOpcode::IMPLICIT_DEF, DL,
                                       MVT::i64);
      In64 = DAG.getTargetInsertSubreg(SystemZ::subreg_h32, DL,
                                       MVT::i64, SDValue(U64, 0), In);
    } else {
      In64 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, In);
      In64 = DAG.getNode(ISD::SHL, DL, MVT::i64, In64,
                         DAG.getConstant(32, DL, MVT::i64));
    }
    SDValue Out64 = DAG.getNode(ISD::BITCAST, DL, MVT::f64, In64);
    return DAG.getTargetExtractSubreg(SystemZ::subreg_r32,
                                      DL, MVT::f32, Out64);
  }
  if (InVT == MVT::f32 && ResVT == MVT::i32) {
    SDNode *U64 = DAG.getMachineNode(TargetOpcode::IMPLICIT_DEF, DL, MVT::f64);
    SDValue In64 = DAG.getTargetInsertSubreg(SystemZ::subreg_r32, DL,
                                             MVT::f64, SDValue(U64, 0), In);
    SDValue Out64 = DAG.getNode(ISD::BITCAST, DL, MVT::i64, In64);
    if (Subtarget.hasHighWord())
      return DAG.getTargetExtractSubreg(SystemZ::subreg_h32, DL,
                                        MVT::i32, Out64);
    SDValue Shift = DAG.getNode(ISD::SRL, DL, MVT::i64, Out64,
                                DAG.getConstant(32, DL, MVT::i64));
    return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Shift);
  }
  llvm_unreachable("Unexpected bitcast combination");
}

SDValue SystemZTargetLowering::lowerVASTART(SDValue Op,
                                            SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  SystemZMachineFunctionInfo *FuncInfo =
    MF.getInfo<SystemZMachineFunctionInfo>();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  SDValue Chain   = Op.getOperand(0);
  SDValue Addr    = Op.getOperand(1);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  SDLoc DL(Op);

  // The initial values of each field.
  const unsigned NumFields = 4;
  SDValue Fields[NumFields] = {
    DAG.getConstant(FuncInfo->getVarArgsFirstGPR(), DL, PtrVT),
    DAG.getConstant(FuncInfo->getVarArgsFirstFPR(), DL, PtrVT),
    DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT),
    DAG.getFrameIndex(FuncInfo->getRegSaveFrameIndex(), PtrVT)
  };

  // Store each field into its respective slot.
  SDValue MemOps[NumFields];
  unsigned Offset = 0;
  for (unsigned I = 0; I < NumFields; ++I) {
    SDValue FieldAddr = Addr;
    if (Offset != 0)
      FieldAddr = DAG.getNode(ISD::ADD, DL, PtrVT, FieldAddr,
                              DAG.getIntPtrConstant(Offset, DL));
    MemOps[I] = DAG.getStore(Chain, DL, Fields[I], FieldAddr,
                             MachinePointerInfo(SV, Offset),
                             false, false, 0);
    Offset += 8;
  }
  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOps);
}

SDValue SystemZTargetLowering::lowerVACOPY(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDValue Chain      = Op.getOperand(0);
  SDValue DstPtr     = Op.getOperand(1);
  SDValue SrcPtr     = Op.getOperand(2);
  const Value *DstSV = cast<SrcValueSDNode>(Op.getOperand(3))->getValue();
  const Value *SrcSV = cast<SrcValueSDNode>(Op.getOperand(4))->getValue();
  SDLoc DL(Op);

  return DAG.getMemcpy(Chain, DL, DstPtr, SrcPtr, DAG.getIntPtrConstant(32, DL),
                       /*Align*/8, /*isVolatile*/false, /*AlwaysInline*/false,
                       /*isTailCall*/false,
                       MachinePointerInfo(DstSV), MachinePointerInfo(SrcSV));
}

SDValue SystemZTargetLowering::
lowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const {
  const TargetFrameLowering *TFI = Subtarget.getFrameLowering();
  bool RealignOpt = !DAG.getMachineFunction().getFunction()->
    hasFnAttribute("no-realign-stack");

  SDValue Chain = Op.getOperand(0);
  SDValue Size  = Op.getOperand(1);
  SDValue Align = Op.getOperand(2);
  SDLoc DL(Op);

  // If user has set the no alignment function attribute, ignore
  // alloca alignments.
  uint64_t AlignVal = (RealignOpt ?
                       dyn_cast<ConstantSDNode>(Align)->getZExtValue() : 0);

  uint64_t StackAlign = TFI->getStackAlignment();
  uint64_t RequiredAlign = std::max(AlignVal, StackAlign);
  uint64_t ExtraAlignSpace = RequiredAlign - StackAlign;

  unsigned SPReg = getStackPointerRegisterToSaveRestore();
  SDValue NeededSpace = Size;

  // Get a reference to the stack pointer.
  SDValue OldSP = DAG.getCopyFromReg(Chain, DL, SPReg, MVT::i64);

  // Add extra space for alignment if needed.
  if (ExtraAlignSpace)
    NeededSpace = DAG.getNode(ISD::ADD, DL, MVT::i64, NeededSpace,
                              DAG.getConstant(ExtraAlignSpace, DL, MVT::i64)); 

  // Get the new stack pointer value.
  SDValue NewSP = DAG.getNode(ISD::SUB, DL, MVT::i64, OldSP, NeededSpace);

  // Copy the new stack pointer back.
  Chain = DAG.getCopyToReg(Chain, DL, SPReg, NewSP);

  // The allocated data lives above the 160 bytes allocated for the standard
  // frame, plus any outgoing stack arguments.  We don't know how much that
  // amounts to yet, so emit a special ADJDYNALLOC placeholder.
  SDValue ArgAdjust = DAG.getNode(SystemZISD::ADJDYNALLOC, DL, MVT::i64);
  SDValue Result = DAG.getNode(ISD::ADD, DL, MVT::i64, NewSP, ArgAdjust);

  // Dynamically realign if needed.
  if (RequiredAlign > StackAlign) {
    Result =
      DAG.getNode(ISD::ADD, DL, MVT::i64, Result,
                  DAG.getConstant(ExtraAlignSpace, DL, MVT::i64));
    Result =
      DAG.getNode(ISD::AND, DL, MVT::i64, Result,
                  DAG.getConstant(~(RequiredAlign - 1), DL, MVT::i64));
  }

  SDValue Ops[2] = { Result, Chain };
  return DAG.getMergeValues(Ops, DL);
}

SDValue SystemZTargetLowering::lowerSMUL_LOHI(SDValue Op,
                                              SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  SDValue Ops[2];
  if (is32Bit(VT))
    // Just do a normal 64-bit multiplication and extract the results.
    // We define this so that it can be used for constant division.
    lowerMUL_LOHI32(DAG, DL, ISD::SIGN_EXTEND, Op.getOperand(0),
                    Op.getOperand(1), Ops[1], Ops[0]);
  else {
    // Do a full 128-bit multiplication based on UMUL_LOHI64:
    //
    //   (ll * rl) + ((lh * rl) << 64) + ((ll * rh) << 64)
    //
    // but using the fact that the upper halves are either all zeros
    // or all ones:
    //
    //   (ll * rl) - ((lh & rl) << 64) - ((ll & rh) << 64)
    //
    // and grouping the right terms together since they are quicker than the
    // multiplication:
    //
    //   (ll * rl) - (((lh & rl) + (ll & rh)) << 64)
    SDValue C63 = DAG.getConstant(63, DL, MVT::i64);
    SDValue LL = Op.getOperand(0);
    SDValue RL = Op.getOperand(1);
    SDValue LH = DAG.getNode(ISD::SRA, DL, VT, LL, C63);
    SDValue RH = DAG.getNode(ISD::SRA, DL, VT, RL, C63);
    // UMUL_LOHI64 returns the low result in the odd register and the high
    // result in the even register.  SMUL_LOHI is defined to return the
    // low half first, so the results are in reverse order.
    lowerGR128Binary(DAG, DL, VT, SystemZ::AEXT128_64, SystemZISD::UMUL_LOHI64,
                     LL, RL, Ops[1], Ops[0]);
    SDValue NegLLTimesRH = DAG.getNode(ISD::AND, DL, VT, LL, RH);
    SDValue NegLHTimesRL = DAG.getNode(ISD::AND, DL, VT, LH, RL);
    SDValue NegSum = DAG.getNode(ISD::ADD, DL, VT, NegLLTimesRH, NegLHTimesRL);
    Ops[1] = DAG.getNode(ISD::SUB, DL, VT, Ops[1], NegSum);
  }
  return DAG.getMergeValues(Ops, DL);
}

SDValue SystemZTargetLowering::lowerUMUL_LOHI(SDValue Op,
                                              SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  SDValue Ops[2];
  if (is32Bit(VT))
    // Just do a normal 64-bit multiplication and extract the results.
    // We define this so that it can be used for constant division.
    lowerMUL_LOHI32(DAG, DL, ISD::ZERO_EXTEND, Op.getOperand(0),
                    Op.getOperand(1), Ops[1], Ops[0]);
  else
    // UMUL_LOHI64 returns the low result in the odd register and the high
    // result in the even register.  UMUL_LOHI is defined to return the
    // low half first, so the results are in reverse order.
    lowerGR128Binary(DAG, DL, VT, SystemZ::AEXT128_64, SystemZISD::UMUL_LOHI64,
                     Op.getOperand(0), Op.getOperand(1), Ops[1], Ops[0]);
  return DAG.getMergeValues(Ops, DL);
}

SDValue SystemZTargetLowering::lowerSDIVREM(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDValue Op0 = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  unsigned Opcode;

  // We use DSGF for 32-bit division.
  if (is32Bit(VT)) {
    Op0 = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, Op0);
    Opcode = SystemZISD::SDIVREM32;
  } else if (DAG.ComputeNumSignBits(Op1) > 32) {
    Op1 = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Op1);
    Opcode = SystemZISD::SDIVREM32;
  } else
    Opcode = SystemZISD::SDIVREM64;

  // DSG(F) takes a 64-bit dividend, so the even register in the GR128
  // input is "don't care".  The instruction returns the remainder in
  // the even register and the quotient in the odd register.
  SDValue Ops[2];
  lowerGR128Binary(DAG, DL, VT, SystemZ::AEXT128_64, Opcode,
                   Op0, Op1, Ops[1], Ops[0]);
  return DAG.getMergeValues(Ops, DL);
}

SDValue SystemZTargetLowering::lowerUDIVREM(SDValue Op,
                                            SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc DL(Op);

  // DL(G) uses a double-width dividend, so we need to clear the even
  // register in the GR128 input.  The instruction returns the remainder
  // in the even register and the quotient in the odd register.
  SDValue Ops[2];
  if (is32Bit(VT))
    lowerGR128Binary(DAG, DL, VT, SystemZ::ZEXT128_32, SystemZISD::UDIVREM32,
                     Op.getOperand(0), Op.getOperand(1), Ops[1], Ops[0]);
  else
    lowerGR128Binary(DAG, DL, VT, SystemZ::ZEXT128_64, SystemZISD::UDIVREM64,
                     Op.getOperand(0), Op.getOperand(1), Ops[1], Ops[0]);
  return DAG.getMergeValues(Ops, DL);
}

SDValue SystemZTargetLowering::lowerOR(SDValue Op, SelectionDAG &DAG) const {
  assert(Op.getValueType() == MVT::i64 && "Should be 64-bit operation");

  // Get the known-zero masks for each operand.
  SDValue Ops[] = { Op.getOperand(0), Op.getOperand(1) };
  APInt KnownZero[2], KnownOne[2];
  DAG.computeKnownBits(Ops[0], KnownZero[0], KnownOne[0]);
  DAG.computeKnownBits(Ops[1], KnownZero[1], KnownOne[1]);

  // See if the upper 32 bits of one operand and the lower 32 bits of the
  // other are known zero.  They are the low and high operands respectively.
  uint64_t Masks[] = { KnownZero[0].getZExtValue(),
                       KnownZero[1].getZExtValue() };
  unsigned High, Low;
  if ((Masks[0] >> 32) == 0xffffffff && uint32_t(Masks[1]) == 0xffffffff)
    High = 1, Low = 0;
  else if ((Masks[1] >> 32) == 0xffffffff && uint32_t(Masks[0]) == 0xffffffff)
    High = 0, Low = 1;
  else
    return Op;

  SDValue LowOp = Ops[Low];
  SDValue HighOp = Ops[High];

  // If the high part is a constant, we're better off using IILH.
  if (HighOp.getOpcode() == ISD::Constant)
    return Op;

  // If the low part is a constant that is outside the range of LHI,
  // then we're better off using IILF.
  if (LowOp.getOpcode() == ISD::Constant) {
    int64_t Value = int32_t(cast<ConstantSDNode>(LowOp)->getZExtValue());
    if (!isInt<16>(Value))
      return Op;
  }

  // Check whether the high part is an AND that doesn't change the
  // high 32 bits and just masks out low bits.  We can skip it if so.
  if (HighOp.getOpcode() == ISD::AND &&
      HighOp.getOperand(1).getOpcode() == ISD::Constant) {
    SDValue HighOp0 = HighOp.getOperand(0);
    uint64_t Mask = cast<ConstantSDNode>(HighOp.getOperand(1))->getZExtValue();
    if (DAG.MaskedValueIsZero(HighOp0, APInt(64, ~(Mask | 0xffffffff))))
      HighOp = HighOp0;
  }

  // Take advantage of the fact that all GR32 operations only change the
  // low 32 bits by truncating Low to an i32 and inserting it directly
  // using a subreg.  The interesting cases are those where the truncation
  // can be folded.
  SDLoc DL(Op);
  SDValue Low32 = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, LowOp);
  return DAG.getTargetInsertSubreg(SystemZ::subreg_l32, DL,
                                   MVT::i64, HighOp, Low32);
}

SDValue SystemZTargetLowering::lowerCTPOP(SDValue Op,
                                          SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  Op = Op.getOperand(0);

  // Handle vector types via VPOPCT.
  if (VT.isVector()) {
    Op = DAG.getNode(ISD::BITCAST, DL, MVT::v16i8, Op);
    Op = DAG.getNode(SystemZISD::POPCNT, DL, MVT::v16i8, Op);
    switch (VT.getVectorElementType().getSizeInBits()) {
    case 8:
      break;
    case 16: {
      Op = DAG.getNode(ISD::BITCAST, DL, VT, Op);
      SDValue Shift = DAG.getConstant(8, DL, MVT::i32);
      SDValue Tmp = DAG.getNode(SystemZISD::VSHL_BY_SCALAR, DL, VT, Op, Shift);
      Op = DAG.getNode(ISD::ADD, DL, VT, Op, Tmp);
      Op = DAG.getNode(SystemZISD::VSRL_BY_SCALAR, DL, VT, Op, Shift);
      break;
    }
    case 32: {
      SDValue Tmp = DAG.getNode(SystemZISD::BYTE_MASK, DL, MVT::v16i8,
                                DAG.getConstant(0, DL, MVT::i32));
      Op = DAG.getNode(SystemZISD::VSUM, DL, VT, Op, Tmp);
      break;
    }
    case 64: {
      SDValue Tmp = DAG.getNode(SystemZISD::BYTE_MASK, DL, MVT::v16i8,
                                DAG.getConstant(0, DL, MVT::i32));
      Op = DAG.getNode(SystemZISD::VSUM, DL, MVT::v4i32, Op, Tmp);
      Op = DAG.getNode(SystemZISD::VSUM, DL, VT, Op, Tmp);
      break;
    }
    default:
      llvm_unreachable("Unexpected type");
    }
    return Op;
  }

  // Get the known-zero mask for the operand.
  APInt KnownZero, KnownOne;
  DAG.computeKnownBits(Op, KnownZero, KnownOne);
  unsigned NumSignificantBits = (~KnownZero).getActiveBits();
  if (NumSignificantBits == 0)
    return DAG.getConstant(0, DL, VT);

  // Skip known-zero high parts of the operand.
  int64_t OrigBitSize = VT.getSizeInBits();
  int64_t BitSize = (int64_t)1 << Log2_32_Ceil(NumSignificantBits);
  BitSize = std::min(BitSize, OrigBitSize);

  // The POPCNT instruction counts the number of bits in each byte.
  Op = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op);
  Op = DAG.getNode(SystemZISD::POPCNT, DL, MVT::i64, Op);
  Op = DAG.getNode(ISD::TRUNCATE, DL, VT, Op);

  // Add up per-byte counts in a binary tree.  All bits of Op at
  // position larger than BitSize remain zero throughout.
  for (int64_t I = BitSize / 2; I >= 8; I = I / 2) {
    SDValue Tmp = DAG.getNode(ISD::SHL, DL, VT, Op, DAG.getConstant(I, DL, VT));
    if (BitSize != OrigBitSize)
      Tmp = DAG.getNode(ISD::AND, DL, VT, Tmp,
                        DAG.getConstant(((uint64_t)1 << BitSize) - 1, DL, VT));
    Op = DAG.getNode(ISD::ADD, DL, VT, Op, Tmp);
  }

  // Extract overall result from high byte.
  if (BitSize > 8)
    Op = DAG.getNode(ISD::SRL, DL, VT, Op,
                     DAG.getConstant(BitSize - 8, DL, VT));

  return Op;
}

// Op is an atomic load.  Lower it into a normal volatile load.
SDValue SystemZTargetLowering::lowerATOMIC_LOAD(SDValue Op,
                                                SelectionDAG &DAG) const {
  auto *Node = cast<AtomicSDNode>(Op.getNode());
  return DAG.getExtLoad(ISD::EXTLOAD, SDLoc(Op), Op.getValueType(),
                        Node->getChain(), Node->getBasePtr(),
                        Node->getMemoryVT(), Node->getMemOperand());
}

// Op is an atomic store.  Lower it into a normal volatile store followed
// by a serialization.
SDValue SystemZTargetLowering::lowerATOMIC_STORE(SDValue Op,
                                                 SelectionDAG &DAG) const {
  auto *Node = cast<AtomicSDNode>(Op.getNode());
  SDValue Chain = DAG.getTruncStore(Node->getChain(), SDLoc(Op), Node->getVal(),
                                    Node->getBasePtr(), Node->getMemoryVT(),
                                    Node->getMemOperand());
  return SDValue(DAG.getMachineNode(SystemZ::Serialize, SDLoc(Op), MVT::Other,
                                    Chain), 0);
}

// Op is an 8-, 16-bit or 32-bit ATOMIC_LOAD_* operation.  Lower the first
// two into the fullword ATOMIC_LOADW_* operation given by Opcode.
SDValue SystemZTargetLowering::lowerATOMIC_LOAD_OP(SDValue Op,
                                                   SelectionDAG &DAG,
                                                   unsigned Opcode) const {
  auto *Node = cast<AtomicSDNode>(Op.getNode());

  // 32-bit operations need no code outside the main loop.
  EVT NarrowVT = Node->getMemoryVT();
  EVT WideVT = MVT::i32;
  if (NarrowVT == WideVT)
    return Op;

  int64_t BitSize = NarrowVT.getSizeInBits();
  SDValue ChainIn = Node->getChain();
  SDValue Addr = Node->getBasePtr();
  SDValue Src2 = Node->getVal();
  MachineMemOperand *MMO = Node->getMemOperand();
  SDLoc DL(Node);
  EVT PtrVT = Addr.getValueType();

  // Convert atomic subtracts of constants into additions.
  if (Opcode == SystemZISD::ATOMIC_LOADW_SUB)
    if (auto *Const = dyn_cast<ConstantSDNode>(Src2)) {
      Opcode = SystemZISD::ATOMIC_LOADW_ADD;
      Src2 = DAG.getConstant(-Const->getSExtValue(), DL, Src2.getValueType());
    }

  // Get the address of the containing word.
  SDValue AlignedAddr = DAG.getNode(ISD::AND, DL, PtrVT, Addr,
                                    DAG.getConstant(-4, DL, PtrVT));

  // Get the number of bits that the word must be rotated left in order
  // to bring the field to the top bits of a GR32.
  SDValue BitShift = DAG.getNode(ISD::SHL, DL, PtrVT, Addr,
                                 DAG.getConstant(3, DL, PtrVT));
  BitShift = DAG.getNode(ISD::TRUNCATE, DL, WideVT, BitShift);

  // Get the complementing shift amount, for rotating a field in the top
  // bits back to its proper position.
  SDValue NegBitShift = DAG.getNode(ISD::SUB, DL, WideVT,
                                    DAG.getConstant(0, DL, WideVT), BitShift);

  // Extend the source operand to 32 bits and prepare it for the inner loop.
  // ATOMIC_SWAPW uses RISBG to rotate the field left, but all other
  // operations require the source to be shifted in advance.  (This shift
  // can be folded if the source is constant.)  For AND and NAND, the lower
  // bits must be set, while for other opcodes they should be left clear.
  if (Opcode != SystemZISD::ATOMIC_SWAPW)
    Src2 = DAG.getNode(ISD::SHL, DL, WideVT, Src2,
                       DAG.getConstant(32 - BitSize, DL, WideVT));
  if (Opcode == SystemZISD::ATOMIC_LOADW_AND ||
      Opcode == SystemZISD::ATOMIC_LOADW_NAND)
    Src2 = DAG.getNode(ISD::OR, DL, WideVT, Src2,
                       DAG.getConstant(uint32_t(-1) >> BitSize, DL, WideVT));

  // Construct the ATOMIC_LOADW_* node.
  SDVTList VTList = DAG.getVTList(WideVT, MVT::Other);
  SDValue Ops[] = { ChainIn, AlignedAddr, Src2, BitShift, NegBitShift,
                    DAG.getConstant(BitSize, DL, WideVT) };
  SDValue AtomicOp = DAG.getMemIntrinsicNode(Opcode, DL, VTList, Ops,
                                             NarrowVT, MMO);

  // Rotate the result of the final CS so that the field is in the lower
  // bits of a GR32, then truncate it.
  SDValue ResultShift = DAG.getNode(ISD::ADD, DL, WideVT, BitShift,
                                    DAG.getConstant(BitSize, DL, WideVT));
  SDValue Result = DAG.getNode(ISD::ROTL, DL, WideVT, AtomicOp, ResultShift);

  SDValue RetOps[2] = { Result, AtomicOp.getValue(1) };
  return DAG.getMergeValues(RetOps, DL);
}

// Op is an ATOMIC_LOAD_SUB operation.  Lower 8- and 16-bit operations
// into ATOMIC_LOADW_SUBs and decide whether to convert 32- and 64-bit
// operations into additions.
SDValue SystemZTargetLowering::lowerATOMIC_LOAD_SUB(SDValue Op,
                                                    SelectionDAG &DAG) const {
  auto *Node = cast<AtomicSDNode>(Op.getNode());
  EVT MemVT = Node->getMemoryVT();
  if (MemVT == MVT::i32 || MemVT == MVT::i64) {
    // A full-width operation.
    assert(Op.getValueType() == MemVT && "Mismatched VTs");
    SDValue Src2 = Node->getVal();
    SDValue NegSrc2;
    SDLoc DL(Src2);

    if (auto *Op2 = dyn_cast<ConstantSDNode>(Src2)) {
      // Use an addition if the operand is constant and either LAA(G) is
      // available or the negative value is in the range of A(G)FHI.
      int64_t Value = (-Op2->getAPIntValue()).getSExtValue();
      if (isInt<32>(Value) || Subtarget.hasInterlockedAccess1())
        NegSrc2 = DAG.getConstant(Value, DL, MemVT);
    } else if (Subtarget.hasInterlockedAccess1())
      // Use LAA(G) if available.
      NegSrc2 = DAG.getNode(ISD::SUB, DL, MemVT, DAG.getConstant(0, DL, MemVT),
                            Src2);

    if (NegSrc2.getNode())
      return DAG.getAtomic(ISD::ATOMIC_LOAD_ADD, DL, MemVT,
                           Node->getChain(), Node->getBasePtr(), NegSrc2,
                           Node->getMemOperand(), Node->getOrdering(),
                           Node->getSynchScope());

    // Use the node as-is.
    return Op;
  }

  return lowerATOMIC_LOAD_OP(Op, DAG, SystemZISD::ATOMIC_LOADW_SUB);
}

// Node is an 8- or 16-bit ATOMIC_CMP_SWAP operation.  Lower the first two
// into a fullword ATOMIC_CMP_SWAPW operation.
SDValue SystemZTargetLowering::lowerATOMIC_CMP_SWAP(SDValue Op,
                                                    SelectionDAG &DAG) const {
  auto *Node = cast<AtomicSDNode>(Op.getNode());

  // We have native support for 32-bit compare and swap.
  EVT NarrowVT = Node->getMemoryVT();
  EVT WideVT = MVT::i32;
  if (NarrowVT == WideVT)
    return Op;

  int64_t BitSize = NarrowVT.getSizeInBits();
  SDValue ChainIn = Node->getOperand(0);
  SDValue Addr = Node->getOperand(1);
  SDValue CmpVal = Node->getOperand(2);
  SDValue SwapVal = Node->getOperand(3);
  MachineMemOperand *MMO = Node->getMemOperand();
  SDLoc DL(Node);
  EVT PtrVT = Addr.getValueType();

  // Get the address of the containing word.
  SDValue AlignedAddr = DAG.getNode(ISD::AND, DL, PtrVT, Addr,
                                    DAG.getConstant(-4, DL, PtrVT));

  // Get the number of bits that the word must be rotated left in order
  // to bring the field to the top bits of a GR32.
  SDValue BitShift = DAG.getNode(ISD::SHL, DL, PtrVT, Addr,
                                 DAG.getConstant(3, DL, PtrVT));
  BitShift = DAG.getNode(ISD::TRUNCATE, DL, WideVT, BitShift);

  // Get the complementing shift amount, for rotating a field in the top
  // bits back to its proper position.
  SDValue NegBitShift = DAG.getNode(ISD::SUB, DL, WideVT,
                                    DAG.getConstant(0, DL, WideVT), BitShift);

  // Construct the ATOMIC_CMP_SWAPW node.
  SDVTList VTList = DAG.getVTList(WideVT, MVT::Other);
  SDValue Ops[] = { ChainIn, AlignedAddr, CmpVal, SwapVal, BitShift,
                    NegBitShift, DAG.getConstant(BitSize, DL, WideVT) };
  SDValue AtomicOp = DAG.getMemIntrinsicNode(SystemZISD::ATOMIC_CMP_SWAPW, DL,
                                             VTList, Ops, NarrowVT, MMO);
  return AtomicOp;
}

SDValue SystemZTargetLowering::lowerSTACKSAVE(SDValue Op,
                                              SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MF.getInfo<SystemZMachineFunctionInfo>()->setManipulatesSP(true);
  return DAG.getCopyFromReg(Op.getOperand(0), SDLoc(Op),
                            SystemZ::R15D, Op.getValueType());
}

SDValue SystemZTargetLowering::lowerSTACKRESTORE(SDValue Op,
                                                 SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MF.getInfo<SystemZMachineFunctionInfo>()->setManipulatesSP(true);
  return DAG.getCopyToReg(Op.getOperand(0), SDLoc(Op),
                          SystemZ::R15D, Op.getOperand(1));
}

SDValue SystemZTargetLowering::lowerPREFETCH(SDValue Op,
                                             SelectionDAG &DAG) const {
  bool IsData = cast<ConstantSDNode>(Op.getOperand(4))->getZExtValue();
  if (!IsData)
    // Just preserve the chain.
    return Op.getOperand(0);

  SDLoc DL(Op);
  bool IsWrite = cast<ConstantSDNode>(Op.getOperand(2))->getZExtValue();
  unsigned Code = IsWrite ? SystemZ::PFD_WRITE : SystemZ::PFD_READ;
  auto *Node = cast<MemIntrinsicSDNode>(Op.getNode());
  SDValue Ops[] = {
    Op.getOperand(0),
    DAG.getConstant(Code, DL, MVT::i32),
    Op.getOperand(1)
  };
  return DAG.getMemIntrinsicNode(SystemZISD::PREFETCH, DL,
                                 Node->getVTList(), Ops,
                                 Node->getMemoryVT(), Node->getMemOperand());
}

// Return an i32 that contains the value of CC immediately after After,
// whose final operand must be MVT::Glue.
static SDValue getCCResult(SelectionDAG &DAG, SDNode *After) {
  SDLoc DL(After);
  SDValue Glue = SDValue(After, After->getNumValues() - 1);
  SDValue IPM = DAG.getNode(SystemZISD::IPM, DL, MVT::i32, Glue);
  return DAG.getNode(ISD::SRL, DL, MVT::i32, IPM,
                     DAG.getConstant(SystemZ::IPM_CC, DL, MVT::i32));
}

SDValue
SystemZTargetLowering::lowerINTRINSIC_W_CHAIN(SDValue Op,
                                              SelectionDAG &DAG) const {
  unsigned Opcode, CCValid;
  if (isIntrinsicWithCCAndChain(Op, Opcode, CCValid)) {
    assert(Op->getNumValues() == 2 && "Expected only CC result and chain");
    SDValue Glued = emitIntrinsicWithChainAndGlue(DAG, Op, Opcode);
    SDValue CC = getCCResult(DAG, Glued.getNode());
    DAG.ReplaceAllUsesOfValueWith(SDValue(Op.getNode(), 0), CC);
    return SDValue();
  }

  return SDValue();
}

SDValue
SystemZTargetLowering::lowerINTRINSIC_WO_CHAIN(SDValue Op,
                                               SelectionDAG &DAG) const {
  unsigned Opcode, CCValid;
  if (isIntrinsicWithCC(Op, Opcode, CCValid)) {
    SDValue Glued = emitIntrinsicWithGlue(DAG, Op, Opcode);
    SDValue CC = getCCResult(DAG, Glued.getNode());
    if (Op->getNumValues() == 1)
      return CC;
    assert(Op->getNumValues() == 2 && "Expected a CC and non-CC result");
    return DAG.getNode(ISD::MERGE_VALUES, SDLoc(Op), Op->getVTList(), Glued,
                       CC);
  }

  unsigned Id = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  switch (Id) {
  case Intrinsic::s390_vpdi:
    return DAG.getNode(SystemZISD::PERMUTE_DWORDS, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3));

  case Intrinsic::s390_vperm:
    return DAG.getNode(SystemZISD::PERMUTE, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3));

  case Intrinsic::s390_vuphb:
  case Intrinsic::s390_vuphh:
  case Intrinsic::s390_vuphf:
    return DAG.getNode(SystemZISD::UNPACK_HIGH, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1));

  case Intrinsic::s390_vuplhb:
  case Intrinsic::s390_vuplhh:
  case Intrinsic::s390_vuplhf:
    return DAG.getNode(SystemZISD::UNPACKL_HIGH, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1));

  case Intrinsic::s390_vuplb:
  case Intrinsic::s390_vuplhw:
  case Intrinsic::s390_vuplf:
    return DAG.getNode(SystemZISD::UNPACK_LOW, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1));

  case Intrinsic::s390_vupllb:
  case Intrinsic::s390_vupllh:
  case Intrinsic::s390_vupllf:
    return DAG.getNode(SystemZISD::UNPACKL_LOW, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1));

  case Intrinsic::s390_vsumb:
  case Intrinsic::s390_vsumh:
  case Intrinsic::s390_vsumgh:
  case Intrinsic::s390_vsumgf:
  case Intrinsic::s390_vsumqf:
  case Intrinsic::s390_vsumqg:
    return DAG.getNode(SystemZISD::VSUM, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  }

  return SDValue();
}

namespace {
// Says that SystemZISD operation Opcode can be used to perform the equivalent
// of a VPERM with permute vector Bytes.  If Opcode takes three operands,
// Operand is the constant third operand, otherwise it is the number of
// bytes in each element of the result.
struct Permute {
  unsigned Opcode;
  unsigned Operand;
  unsigned char Bytes[SystemZ::VectorBytes];
};
}

static const Permute PermuteForms[] = {
  // VMRHG
  { SystemZISD::MERGE_HIGH, 8,
    { 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23 } },
  // VMRHF
  { SystemZISD::MERGE_HIGH, 4,
    { 0, 1, 2, 3, 16, 17, 18, 19, 4, 5, 6, 7, 20, 21, 22, 23 } },
  // VMRHH
  { SystemZISD::MERGE_HIGH, 2,
    { 0, 1, 16, 17, 2, 3, 18, 19, 4, 5, 20, 21, 6, 7, 22, 23 } },
  // VMRHB
  { SystemZISD::MERGE_HIGH, 1,
    { 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23 } },
  // VMRLG
  { SystemZISD::MERGE_LOW, 8,
    { 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31 } },
  // VMRLF
  { SystemZISD::MERGE_LOW, 4,
    { 8, 9, 10, 11, 24, 25, 26, 27, 12, 13, 14, 15, 28, 29, 30, 31 } },
  // VMRLH
  { SystemZISD::MERGE_LOW, 2,
    { 8, 9, 24, 25, 10, 11, 26, 27, 12, 13, 28, 29, 14, 15, 30, 31 } },
  // VMRLB
  { SystemZISD::MERGE_LOW, 1,
    { 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31 } },
  // VPKG
  { SystemZISD::PACK, 4,
    { 4, 5, 6, 7, 12, 13, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31 } },
  // VPKF
  { SystemZISD::PACK, 2,
    { 2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31 } },
  // VPKH
  { SystemZISD::PACK, 1,
    { 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31 } },
  // VPDI V1, V2, 4  (low half of V1, high half of V2)
  { SystemZISD::PERMUTE_DWORDS, 4,
    { 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 } },
  // VPDI V1, V2, 1  (high half of V1, low half of V2)
  { SystemZISD::PERMUTE_DWORDS, 1,
    { 0, 1, 2, 3, 4, 5, 6, 7, 24, 25, 26, 27, 28, 29, 30, 31 } }
};

// Called after matching a vector shuffle against a particular pattern.
// Both the original shuffle and the pattern have two vector operands.
// OpNos[0] is the operand of the original shuffle that should be used for
// operand 0 of the pattern, or -1 if operand 0 of the pattern can be anything.
// OpNos[1] is the same for operand 1 of the pattern.  Resolve these -1s and
// set OpNo0 and OpNo1 to the shuffle operands that should actually be used
// for operands 0 and 1 of the pattern.
static bool chooseShuffleOpNos(int *OpNos, unsigned &OpNo0, unsigned &OpNo1) {
  if (OpNos[0] < 0) {
    if (OpNos[1] < 0)
      return false;
    OpNo0 = OpNo1 = OpNos[1];
  } else if (OpNos[1] < 0) {
    OpNo0 = OpNo1 = OpNos[0];
  } else {
    OpNo0 = OpNos[0];
    OpNo1 = OpNos[1];
  }
  return true;
}

// Bytes is a VPERM-like permute vector, except that -1 is used for
// undefined bytes.  Return true if the VPERM can be implemented using P.
// When returning true set OpNo0 to the VPERM operand that should be
// used for operand 0 of P and likewise OpNo1 for operand 1 of P.
//
// For example, if swapping the VPERM operands allows P to match, OpNo0
// will be 1 and OpNo1 will be 0.  If instead Bytes only refers to one
// operand, but rewriting it to use two duplicated operands allows it to
// match P, then OpNo0 and OpNo1 will be the same.
static bool matchPermute(const SmallVectorImpl<int> &Bytes, const Permute &P,
                         unsigned &OpNo0, unsigned &OpNo1) {
  int OpNos[] = { -1, -1 };
  for (unsigned I = 0; I < SystemZ::VectorBytes; ++I) {
    int Elt = Bytes[I];
    if (Elt >= 0) {
      // Make sure that the two permute vectors use the same suboperand
      // byte number.  Only the operand numbers (the high bits) are
      // allowed to differ.
      if ((Elt ^ P.Bytes[I]) & (SystemZ::VectorBytes - 1))
        return false;
      int ModelOpNo = P.Bytes[I] / SystemZ::VectorBytes;
      int RealOpNo = unsigned(Elt) / SystemZ::VectorBytes;
      // Make sure that the operand mappings are consistent with previous
      // elements.
      if (OpNos[ModelOpNo] == 1 - RealOpNo)
        return false;
      OpNos[ModelOpNo] = RealOpNo;
    }
  }
  return chooseShuffleOpNos(OpNos, OpNo0, OpNo1);
}

// As above, but search for a matching permute.
static const Permute *matchPermute(const SmallVectorImpl<int> &Bytes,
                                   unsigned &OpNo0, unsigned &OpNo1) {
  for (auto &P : PermuteForms)
    if (matchPermute(Bytes, P, OpNo0, OpNo1))
      return &P;
  return nullptr;
}

// Bytes is a VPERM-like permute vector, except that -1 is used for
// undefined bytes.  This permute is an operand of an outer permute.
// See whether redistributing the -1 bytes gives a shuffle that can be
// implemented using P.  If so, set Transform to a VPERM-like permute vector
// that, when applied to the result of P, gives the original permute in Bytes.
static bool matchDoublePermute(const SmallVectorImpl<int> &Bytes,
                               const Permute &P,
                               SmallVectorImpl<int> &Transform) {
  unsigned To = 0;
  for (unsigned From = 0; From < SystemZ::VectorBytes; ++From) {
    int Elt = Bytes[From];
    if (Elt < 0)
      // Byte number From of the result is undefined.
      Transform[From] = -1;
    else {
      while (P.Bytes[To] != Elt) {
        To += 1;
        if (To == SystemZ::VectorBytes)
          return false;
      }
      Transform[From] = To;
    }
  }
  return true;
}

// As above, but search for a matching permute.
static const Permute *matchDoublePermute(const SmallVectorImpl<int> &Bytes,
                                         SmallVectorImpl<int> &Transform) {
  for (auto &P : PermuteForms)
    if (matchDoublePermute(Bytes, P, Transform))
      return &P;
  return nullptr;
}

// Convert the mask of the given VECTOR_SHUFFLE into a byte-level mask,
// as if it had type vNi8.
static void getVPermMask(ShuffleVectorSDNode *VSN,
                         SmallVectorImpl<int> &Bytes) {
  EVT VT = VSN->getValueType(0);
  unsigned NumElements = VT.getVectorNumElements();
  unsigned BytesPerElement = VT.getVectorElementType().getStoreSize();
  Bytes.resize(NumElements * BytesPerElement, -1);
  for (unsigned I = 0; I < NumElements; ++I) {
    int Index = VSN->getMaskElt(I);
    if (Index >= 0)
      for (unsigned J = 0; J < BytesPerElement; ++J)
        Bytes[I * BytesPerElement + J] = Index * BytesPerElement + J;
  }
}

// Bytes is a VPERM-like permute vector, except that -1 is used for
// undefined bytes.  See whether bytes [Start, Start + BytesPerElement) of
// the result come from a contiguous sequence of bytes from one input.
// Set Base to the selector for the first byte if so.
static bool getShuffleInput(const SmallVectorImpl<int> &Bytes, unsigned Start,
                            unsigned BytesPerElement, int &Base) {
  Base = -1;
  for (unsigned I = 0; I < BytesPerElement; ++I) {
    if (Bytes[Start + I] >= 0) {
      unsigned Elem = Bytes[Start + I];
      if (Base < 0) {
        Base = Elem - I;
        // Make sure the bytes would come from one input operand.
        if (unsigned(Base) % Bytes.size() + BytesPerElement > Bytes.size())
          return false;
      } else if (unsigned(Base) != Elem - I)
        return false;
    }
  }
  return true;
}

// Bytes is a VPERM-like permute vector, except that -1 is used for
// undefined bytes.  Return true if it can be performed using VSLDI.
// When returning true, set StartIndex to the shift amount and OpNo0
// and OpNo1 to the VPERM operands that should be used as the first
// and second shift operand respectively.
static bool isShlDoublePermute(const SmallVectorImpl<int> &Bytes,
                               unsigned &StartIndex, unsigned &OpNo0,
                               unsigned &OpNo1) {
  int OpNos[] = { -1, -1 };
  int Shift = -1;
  for (unsigned I = 0; I < 16; ++I) {
    int Index = Bytes[I];
    if (Index >= 0) {
      int ExpectedShift = (Index - I) % SystemZ::VectorBytes;
      int ModelOpNo = unsigned(ExpectedShift + I) / SystemZ::VectorBytes;
      int RealOpNo = unsigned(Index) / SystemZ::VectorBytes;
      if (Shift < 0)
        Shift = ExpectedShift;
      else if (Shift != ExpectedShift)
        return false;
      // Make sure that the operand mappings are consistent with previous
      // elements.
      if (OpNos[ModelOpNo] == 1 - RealOpNo)
        return false;
      OpNos[ModelOpNo] = RealOpNo;
    }
  }
  StartIndex = Shift;
  return chooseShuffleOpNos(OpNos, OpNo0, OpNo1);
}

// Create a node that performs P on operands Op0 and Op1, casting the
// operands to the appropriate type.  The type of the result is determined by P.
static SDValue getPermuteNode(SelectionDAG &DAG, SDLoc DL,
                              const Permute &P, SDValue Op0, SDValue Op1) {
  // VPDI (PERMUTE_DWORDS) always operates on v2i64s.  The input
  // elements of a PACK are twice as wide as the outputs.
  unsigned InBytes = (P.Opcode == SystemZISD::PERMUTE_DWORDS ? 8 :
                      P.Opcode == SystemZISD::PACK ? P.Operand * 2 :
                      P.Operand);
  // Cast both operands to the appropriate type.
  MVT InVT = MVT::getVectorVT(MVT::getIntegerVT(InBytes * 8),
                              SystemZ::VectorBytes / InBytes);
  Op0 = DAG.getNode(ISD::BITCAST, DL, InVT, Op0);
  Op1 = DAG.getNode(ISD::BITCAST, DL, InVT, Op1);
  SDValue Op;
  if (P.Opcode == SystemZISD::PERMUTE_DWORDS) {
    SDValue Op2 = DAG.getConstant(P.Operand, DL, MVT::i32);
    Op = DAG.getNode(SystemZISD::PERMUTE_DWORDS, DL, InVT, Op0, Op1, Op2);
  } else if (P.Opcode == SystemZISD::PACK) {
    MVT OutVT = MVT::getVectorVT(MVT::getIntegerVT(P.Operand * 8),
                                 SystemZ::VectorBytes / P.Operand);
    Op = DAG.getNode(SystemZISD::PACK, DL, OutVT, Op0, Op1);
  } else {
    Op = DAG.getNode(P.Opcode, DL, InVT, Op0, Op1);
  }
  return Op;
}

// Bytes is a VPERM-like permute vector, except that -1 is used for
// undefined bytes.  Implement it on operands Ops[0] and Ops[1] using
// VSLDI or VPERM.
static SDValue getGeneralPermuteNode(SelectionDAG &DAG, SDLoc DL, SDValue *Ops,
                                     const SmallVectorImpl<int> &Bytes) {
  for (unsigned I = 0; I < 2; ++I)
    Ops[I] = DAG.getNode(ISD::BITCAST, DL, MVT::v16i8, Ops[I]);

  // First see whether VSLDI can be used.
  unsigned StartIndex, OpNo0, OpNo1;
  if (isShlDoublePermute(Bytes, StartIndex, OpNo0, OpNo1))
    return DAG.getNode(SystemZISD::SHL_DOUBLE, DL, MVT::v16i8, Ops[OpNo0],
                       Ops[OpNo1], DAG.getConstant(StartIndex, DL, MVT::i32));

  // Fall back on VPERM.  Construct an SDNode for the permute vector.
  SDValue IndexNodes[SystemZ::VectorBytes];
  for (unsigned I = 0; I < SystemZ::VectorBytes; ++I)
    if (Bytes[I] >= 0)
      IndexNodes[I] = DAG.getConstant(Bytes[I], DL, MVT::i32);
    else
      IndexNodes[I] = DAG.getUNDEF(MVT::i32);
  SDValue Op2 = DAG.getNode(ISD::BUILD_VECTOR, DL, MVT::v16i8, IndexNodes);
  return DAG.getNode(SystemZISD::PERMUTE, DL, MVT::v16i8, Ops[0], Ops[1], Op2);
}

namespace {
// Describes a general N-operand vector shuffle.
struct GeneralShuffle {
  GeneralShuffle(EVT vt) : VT(vt) {}
  void addUndef();
  void add(SDValue, unsigned);
  SDValue getNode(SelectionDAG &, SDLoc);

  // The operands of the shuffle.
  SmallVector<SDValue, SystemZ::VectorBytes> Ops;

  // Index I is -1 if byte I of the result is undefined.  Otherwise the
  // result comes from byte Bytes[I] % SystemZ::VectorBytes of operand
  // Bytes[I] / SystemZ::VectorBytes.
  SmallVector<int, SystemZ::VectorBytes> Bytes;

  // The type of the shuffle result.
  EVT VT;
};
}

// Add an extra undefined element to the shuffle.
void GeneralShuffle::addUndef() {
  unsigned BytesPerElement = VT.getVectorElementType().getStoreSize();
  for (unsigned I = 0; I < BytesPerElement; ++I)
    Bytes.push_back(-1);
}

// Add an extra element to the shuffle, taking it from element Elem of Op.
// A null Op indicates a vector input whose value will be calculated later;
// there is at most one such input per shuffle and it always has the same
// type as the result.
void GeneralShuffle::add(SDValue Op, unsigned Elem) {
  unsigned BytesPerElement = VT.getVectorElementType().getStoreSize();

  // The source vector can have wider elements than the result,
  // either through an explicit TRUNCATE or because of type legalization.
  // We want the least significant part.
  EVT FromVT = Op.getNode() ? Op.getValueType() : VT;
  unsigned FromBytesPerElement = FromVT.getVectorElementType().getStoreSize();
  assert(FromBytesPerElement >= BytesPerElement &&
         "Invalid EXTRACT_VECTOR_ELT");
  unsigned Byte = ((Elem * FromBytesPerElement) % SystemZ::VectorBytes +
                   (FromBytesPerElement - BytesPerElement));

  // Look through things like shuffles and bitcasts.
  while (Op.getNode()) {
    if (Op.getOpcode() == ISD::BITCAST)
      Op = Op.getOperand(0);
    else if (Op.getOpcode() == ISD::VECTOR_SHUFFLE && Op.hasOneUse()) {
      // See whether the bytes we need come from a contiguous part of one
      // operand.
      SmallVector<int, SystemZ::VectorBytes> OpBytes;
      getVPermMask(cast<ShuffleVectorSDNode>(Op), OpBytes);
      int NewByte;
      if (!getShuffleInput(OpBytes, Byte, BytesPerElement, NewByte))
        break;
      if (NewByte < 0) {
        addUndef();
        return;
      }
      Op = Op.getOperand(unsigned(NewByte) / SystemZ::VectorBytes);
      Byte = unsigned(NewByte) % SystemZ::VectorBytes;
    } else if (Op.getOpcode() == ISD::UNDEF) {
      addUndef();
      return;
    } else
      break;
  }

  // Make sure that the source of the extraction is in Ops.
  unsigned OpNo = 0;
  for (; OpNo < Ops.size(); ++OpNo)
    if (Ops[OpNo] == Op)
      break;
  if (OpNo == Ops.size())
    Ops.push_back(Op);

  // Add the element to Bytes.
  unsigned Base = OpNo * SystemZ::VectorBytes + Byte;
  for (unsigned I = 0; I < BytesPerElement; ++I)
    Bytes.push_back(Base + I);
}

// Return SDNodes for the completed shuffle.
SDValue GeneralShuffle::getNode(SelectionDAG &DAG, SDLoc DL) {
  assert(Bytes.size() == SystemZ::VectorBytes && "Incomplete vector");

  if (Ops.size() == 0)
    return DAG.getUNDEF(VT);

  // Make sure that there are at least two shuffle operands.
  if (Ops.size() == 1)
    Ops.push_back(DAG.getUNDEF(MVT::v16i8));

  // Create a tree of shuffles, deferring root node until after the loop.
  // Try to redistribute the undefined elements of non-root nodes so that
  // the non-root shuffles match something like a pack or merge, then adjust
  // the parent node's permute vector to compensate for the new order.
  // Among other things, this copes with vectors like <2 x i16> that were
  // padded with undefined elements during type legalization.
  //
  // In the best case this redistribution will lead to the whole tree
  // using packs and merges.  It should rarely be a loss in other cases.
  unsigned Stride = 1;
  for (; Stride * 2 < Ops.size(); Stride *= 2) {
    for (unsigned I = 0; I < Ops.size() - Stride; I += Stride * 2) {
      SDValue SubOps[] = { Ops[I], Ops[I + Stride] };

      // Create a mask for just these two operands.
      SmallVector<int, SystemZ::VectorBytes> NewBytes(SystemZ::VectorBytes);
      for (unsigned J = 0; J < SystemZ::VectorBytes; ++J) {
        unsigned OpNo = unsigned(Bytes[J]) / SystemZ::VectorBytes;
        unsigned Byte = unsigned(Bytes[J]) % SystemZ::VectorBytes;
        if (OpNo == I)
          NewBytes[J] = Byte;
        else if (OpNo == I + Stride)
          NewBytes[J] = SystemZ::VectorBytes + Byte;
        else
          NewBytes[J] = -1;
      }
      // See if it would be better to reorganize NewMask to avoid using VPERM.
      SmallVector<int, SystemZ::VectorBytes> NewBytesMap(SystemZ::VectorBytes);
      if (const Permute *P = matchDoublePermute(NewBytes, NewBytesMap)) {
        Ops[I] = getPermuteNode(DAG, DL, *P, SubOps[0], SubOps[1]);
        // Applying NewBytesMap to Ops[I] gets back to NewBytes.
        for (unsigned J = 0; J < SystemZ::VectorBytes; ++J) {
          if (NewBytes[J] >= 0) {
            assert(unsigned(NewBytesMap[J]) < SystemZ::VectorBytes &&
                   "Invalid double permute");
            Bytes[J] = I * SystemZ::VectorBytes + NewBytesMap[J];
          } else
            assert(NewBytesMap[J] < 0 && "Invalid double permute");
        }
      } else {
        // Just use NewBytes on the operands.
        Ops[I] = getGeneralPermuteNode(DAG, DL, SubOps, NewBytes);
        for (unsigned J = 0; J < SystemZ::VectorBytes; ++J)
          if (NewBytes[J] >= 0)
            Bytes[J] = I * SystemZ::VectorBytes + J;
      }
    }
  }

  // Now we just have 2 inputs.  Put the second operand in Ops[1].
  if (Stride > 1) {
    Ops[1] = Ops[Stride];
    for (unsigned I = 0; I < SystemZ::VectorBytes; ++I)
      if (Bytes[I] >= int(SystemZ::VectorBytes))
        Bytes[I] -= (Stride - 1) * SystemZ::VectorBytes;
  }

  // Look for an instruction that can do the permute without resorting
  // to VPERM.
  unsigned OpNo0, OpNo1;
  SDValue Op;
  if (const Permute *P = matchPermute(Bytes, OpNo0, OpNo1))
    Op = getPermuteNode(DAG, DL, *P, Ops[OpNo0], Ops[OpNo1]);
  else
    Op = getGeneralPermuteNode(DAG, DL, &Ops[0], Bytes);
  return DAG.getNode(ISD::BITCAST, DL, VT, Op);
}

// Return true if the given BUILD_VECTOR is a scalar-to-vector conversion.
static bool isScalarToVector(SDValue Op) {
  for (unsigned I = 1, E = Op.getNumOperands(); I != E; ++I)
    if (Op.getOperand(I).getOpcode() != ISD::UNDEF)
      return false;
  return true;
}

// Return a vector of type VT that contains Value in the first element.
// The other elements don't matter.
static SDValue buildScalarToVector(SelectionDAG &DAG, SDLoc DL, EVT VT,
                                   SDValue Value) {
  // If we have a constant, replicate it to all elements and let the
  // BUILD_VECTOR lowering take care of it.
  if (Value.getOpcode() == ISD::Constant ||
      Value.getOpcode() == ISD::ConstantFP) {
    SmallVector<SDValue, 16> Ops(VT.getVectorNumElements(), Value);
    return DAG.getNode(ISD::BUILD_VECTOR, DL, VT, Ops);
  }
  if (Value.getOpcode() == ISD::UNDEF)
    return DAG.getUNDEF(VT);
  return DAG.getNode(ISD::SCALAR_TO_VECTOR, DL, VT, Value);
}

// Return a vector of type VT in which Op0 is in element 0 and Op1 is in
// element 1.  Used for cases in which replication is cheap.
static SDValue buildMergeScalars(SelectionDAG &DAG, SDLoc DL, EVT VT,
                                 SDValue Op0, SDValue Op1) {
  if (Op0.getOpcode() == ISD::UNDEF) {
    if (Op1.getOpcode() == ISD::UNDEF)
      return DAG.getUNDEF(VT);
    return DAG.getNode(SystemZISD::REPLICATE, DL, VT, Op1);
  }
  if (Op1.getOpcode() == ISD::UNDEF)
    return DAG.getNode(SystemZISD::REPLICATE, DL, VT, Op0);
  return DAG.getNode(SystemZISD::MERGE_HIGH, DL, VT,
                     buildScalarToVector(DAG, DL, VT, Op0),
                     buildScalarToVector(DAG, DL, VT, Op1));
}

// Extend GPR scalars Op0 and Op1 to doublewords and return a v2i64
// vector for them.
static SDValue joinDwords(SelectionDAG &DAG, SDLoc DL, SDValue Op0,
                          SDValue Op1) {
  if (Op0.getOpcode() == ISD::UNDEF && Op1.getOpcode() == ISD::UNDEF)
    return DAG.getUNDEF(MVT::v2i64);
  // If one of the two inputs is undefined then replicate the other one,
  // in order to avoid using another register unnecessarily.
  if (Op0.getOpcode() == ISD::UNDEF)
    Op0 = Op1 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op1);
  else if (Op1.getOpcode() == ISD::UNDEF)
    Op0 = Op1 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op0);
  else {
    Op0 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op0);
    Op1 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Op1);
  }
  return DAG.getNode(SystemZISD::JOIN_DWORDS, DL, MVT::v2i64, Op0, Op1);
}

// Try to represent constant BUILD_VECTOR node BVN using a
// SystemZISD::BYTE_MASK-style mask.  Store the mask value in Mask
// on success.
static bool tryBuildVectorByteMask(BuildVectorSDNode *BVN, uint64_t &Mask) {
  EVT ElemVT = BVN->getValueType(0).getVectorElementType();
  unsigned BytesPerElement = ElemVT.getStoreSize();
  for (unsigned I = 0, E = BVN->getNumOperands(); I != E; ++I) {
    SDValue Op = BVN->getOperand(I);
    if (Op.getOpcode() != ISD::UNDEF) {
      uint64_t Value;
      if (Op.getOpcode() == ISD::Constant)
        Value = dyn_cast<ConstantSDNode>(Op)->getZExtValue();
      else if (Op.getOpcode() == ISD::ConstantFP)
        Value = (dyn_cast<ConstantFPSDNode>(Op)->getValueAPF().bitcastToAPInt()
                 .getZExtValue());
      else
        return false;
      for (unsigned J = 0; J < BytesPerElement; ++J) {
        uint64_t Byte = (Value >> (J * 8)) & 0xff;
        if (Byte == 0xff)
          Mask |= 1ULL << ((E - I - 1) * BytesPerElement + J);
        else if (Byte != 0)
          return false;
      }
    }
  }
  return true;
}

// Try to load a vector constant in which BitsPerElement-bit value Value
// is replicated to fill the vector.  VT is the type of the resulting
// constant, which may have elements of a different size from BitsPerElement.
// Return the SDValue of the constant on success, otherwise return
// an empty value.
static SDValue tryBuildVectorReplicate(SelectionDAG &DAG,
                                       const SystemZInstrInfo *TII,
                                       SDLoc DL, EVT VT, uint64_t Value,
                                       unsigned BitsPerElement) {
  // Signed 16-bit values can be replicated using VREPI.
  int64_t SignedValue = SignExtend64(Value, BitsPerElement);
  if (isInt<16>(SignedValue)) {
    MVT VecVT = MVT::getVectorVT(MVT::getIntegerVT(BitsPerElement),
                                 SystemZ::VectorBits / BitsPerElement);
    SDValue Op = DAG.getNode(SystemZISD::REPLICATE, DL, VecVT,
                             DAG.getConstant(SignedValue, DL, MVT::i32));
    return DAG.getNode(ISD::BITCAST, DL, VT, Op);
  }
  // See whether rotating the constant left some N places gives a value that
  // is one less than a power of 2 (i.e. all zeros followed by all ones).
  // If so we can use VGM.
  unsigned Start, End;
  if (TII->isRxSBGMask(Value, BitsPerElement, Start, End)) {
    // isRxSBGMask returns the bit numbers for a full 64-bit value,
    // with 0 denoting 1 << 63 and 63 denoting 1.  Convert them to
    // bit numbers for an BitsPerElement value, so that 0 denotes
    // 1 << (BitsPerElement-1).
    Start -= 64 - BitsPerElement;
    End -= 64 - BitsPerElement;
    MVT VecVT = MVT::getVectorVT(MVT::getIntegerVT(BitsPerElement),
                                 SystemZ::VectorBits / BitsPerElement);
    SDValue Op = DAG.getNode(SystemZISD::ROTATE_MASK, DL, VecVT,
                             DAG.getConstant(Start, DL, MVT::i32),
                             DAG.getConstant(End, DL, MVT::i32));
    return DAG.getNode(ISD::BITCAST, DL, VT, Op);
  }
  return SDValue();
}

// If a BUILD_VECTOR contains some EXTRACT_VECTOR_ELTs, it's usually
// better to use VECTOR_SHUFFLEs on them, only using BUILD_VECTOR for
// the non-EXTRACT_VECTOR_ELT elements.  See if the given BUILD_VECTOR
// would benefit from this representation and return it if so.
static SDValue tryBuildVectorShuffle(SelectionDAG &DAG,
                                     BuildVectorSDNode *BVN) {
  EVT VT = BVN->getValueType(0);
  unsigned NumElements = VT.getVectorNumElements();

  // Represent the BUILD_VECTOR as an N-operand VECTOR_SHUFFLE-like operation
  // on byte vectors.  If there are non-EXTRACT_VECTOR_ELT elements that still
  // need a BUILD_VECTOR, add an additional placeholder operand for that
  // BUILD_VECTOR and store its operands in ResidueOps.
  GeneralShuffle GS(VT);
  SmallVector<SDValue, SystemZ::VectorBytes> ResidueOps;
  bool FoundOne = false;
  for (unsigned I = 0; I < NumElements; ++I) {
    SDValue Op = BVN->getOperand(I);
    if (Op.getOpcode() == ISD::TRUNCATE)
      Op = Op.getOperand(0);
    if (Op.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
        Op.getOperand(1).getOpcode() == ISD::Constant) {
      unsigned Elem = cast<ConstantSDNode>(Op.getOperand(1))->getZExtValue();
      GS.add(Op.getOperand(0), Elem);
      FoundOne = true;
    } else if (Op.getOpcode() == ISD::UNDEF) {
      GS.addUndef();
    } else {
      GS.add(SDValue(), ResidueOps.size());
      ResidueOps.push_back(BVN->getOperand(I));
    }
  }

  // Nothing to do if there are no EXTRACT_VECTOR_ELTs.
  if (!FoundOne)
    return SDValue();

  // Create the BUILD_VECTOR for the remaining elements, if any.
  if (!ResidueOps.empty()) {
    while (ResidueOps.size() < NumElements)
      ResidueOps.push_back(DAG.getUNDEF(ResidueOps[0].getValueType()));
    for (auto &Op : GS.Ops) {
      if (!Op.getNode()) {
        Op = DAG.getNode(ISD::BUILD_VECTOR, SDLoc(BVN), VT, ResidueOps);
        break;
      }
    }
  }
  return GS.getNode(DAG, SDLoc(BVN));
}

// Combine GPR scalar values Elems into a vector of type VT.
static SDValue buildVector(SelectionDAG &DAG, SDLoc DL, EVT VT,
                           SmallVectorImpl<SDValue> &Elems) {
  // See whether there is a single replicated value.
  SDValue Single;
  unsigned int NumElements = Elems.size();
  unsigned int Count = 0;
  for (auto Elem : Elems) {
    if (Elem.getOpcode() != ISD::UNDEF) {
      if (!Single.getNode())
        Single = Elem;
      else if (Elem != Single) {
        Single = SDValue();
        break;
      }
      Count += 1;
    }
  }
  // There are three cases here:
  //
  // - if the only defined element is a loaded one, the best sequence
  //   is a replicating load.
  //
  // - otherwise, if the only defined element is an i64 value, we will
  //   end up with the same VLVGP sequence regardless of whether we short-cut
  //   for replication or fall through to the later code.
  //
  // - otherwise, if the only defined element is an i32 or smaller value,
  //   we would need 2 instructions to replicate it: VLVGP followed by VREPx.
  //   This is only a win if the single defined element is used more than once.
  //   In other cases we're better off using a single VLVGx.
  if (Single.getNode() && (Count > 1 || Single.getOpcode() == ISD::LOAD))
    return DAG.getNode(SystemZISD::REPLICATE, DL, VT, Single);

  // The best way of building a v2i64 from two i64s is to use VLVGP.
  if (VT == MVT::v2i64)
    return joinDwords(DAG, DL, Elems[0], Elems[1]);

  // Use a 64-bit merge high to combine two doubles.
  if (VT == MVT::v2f64)
    return buildMergeScalars(DAG, DL, VT, Elems[0], Elems[1]);

  // Build v4f32 values directly from the FPRs:
  //
  //   <Axxx> <Bxxx> <Cxxxx> <Dxxx>
  //         V              V         VMRHF
  //      <ABxx>         <CDxx>
  //                V                 VMRHG
  //              <ABCD>
  if (VT == MVT::v4f32) {
    SDValue Op01 = buildMergeScalars(DAG, DL, VT, Elems[0], Elems[1]);
    SDValue Op23 = buildMergeScalars(DAG, DL, VT, Elems[2], Elems[3]);
    // Avoid unnecessary undefs by reusing the other operand.
    if (Op01.getOpcode() == ISD::UNDEF)
      Op01 = Op23;
    else if (Op23.getOpcode() == ISD::UNDEF)
      Op23 = Op01;
    // Merging identical replications is a no-op.
    if (Op01.getOpcode() == SystemZISD::REPLICATE && Op01 == Op23)
      return Op01;
    Op01 = DAG.getNode(ISD::BITCAST, DL, MVT::v2i64, Op01);
    Op23 = DAG.getNode(ISD::BITCAST, DL, MVT::v2i64, Op23);
    SDValue Op = DAG.getNode(SystemZISD::MERGE_HIGH,
                             DL, MVT::v2i64, Op01, Op23);
    return DAG.getNode(ISD::BITCAST, DL, VT, Op);
  }

  // Collect the constant terms.
  SmallVector<SDValue, SystemZ::VectorBytes> Constants(NumElements, SDValue());
  SmallVector<bool, SystemZ::VectorBytes> Done(NumElements, false);

  unsigned NumConstants = 0;
  for (unsigned I = 0; I < NumElements; ++I) {
    SDValue Elem = Elems[I];
    if (Elem.getOpcode() == ISD::Constant ||
        Elem.getOpcode() == ISD::ConstantFP) {
      NumConstants += 1;
      Constants[I] = Elem;
      Done[I] = true;
    }
  }
  // If there was at least one constant, fill in the other elements of
  // Constants with undefs to get a full vector constant and use that
  // as the starting point.
  SDValue Result;
  if (NumConstants > 0) {
    for (unsigned I = 0; I < NumElements; ++I)
      if (!Constants[I].getNode())
        Constants[I] = DAG.getUNDEF(Elems[I].getValueType());
    Result = DAG.getNode(ISD::BUILD_VECTOR, DL, VT, Constants);
  } else {
    // Otherwise try to use VLVGP to start the sequence in order to
    // avoid a false dependency on any previous contents of the vector
    // register.  This only makes sense if one of the associated elements
    // is defined.
    unsigned I1 = NumElements / 2 - 1;
    unsigned I2 = NumElements - 1;
    bool Def1 = (Elems[I1].getOpcode() != ISD::UNDEF);
    bool Def2 = (Elems[I2].getOpcode() != ISD::UNDEF);
    if (Def1 || Def2) {
      SDValue Elem1 = Elems[Def1 ? I1 : I2];
      SDValue Elem2 = Elems[Def2 ? I2 : I1];
      Result = DAG.getNode(ISD::BITCAST, DL, VT,
                           joinDwords(DAG, DL, Elem1, Elem2));
      Done[I1] = true;
      Done[I2] = true;
    } else
      Result = DAG.getUNDEF(VT);
  }

  // Use VLVGx to insert the other elements.
  for (unsigned I = 0; I < NumElements; ++I)
    if (!Done[I] && Elems[I].getOpcode() != ISD::UNDEF)
      Result = DAG.getNode(ISD::INSERT_VECTOR_ELT, DL, VT, Result, Elems[I],
                           DAG.getConstant(I, DL, MVT::i32));
  return Result;
}

SDValue SystemZTargetLowering::lowerBUILD_VECTOR(SDValue Op,
                                                 SelectionDAG &DAG) const {
  const SystemZInstrInfo *TII =
    static_cast<const SystemZInstrInfo *>(Subtarget.getInstrInfo());
  auto *BVN = cast<BuildVectorSDNode>(Op.getNode());
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  if (BVN->isConstant()) {
    // Try using VECTOR GENERATE BYTE MASK.  This is the architecturally-
    // preferred way of creating all-zero and all-one vectors so give it
    // priority over other methods below.
    uint64_t Mask = 0;
    if (tryBuildVectorByteMask(BVN, Mask)) {
      SDValue Op = DAG.getNode(SystemZISD::BYTE_MASK, DL, MVT::v16i8,
                               DAG.getConstant(Mask, DL, MVT::i32));
      return DAG.getNode(ISD::BITCAST, DL, VT, Op);
    }

    // Try using some form of replication.
    APInt SplatBits, SplatUndef;
    unsigned SplatBitSize;
    bool HasAnyUndefs;
    if (BVN->isConstantSplat(SplatBits, SplatUndef, SplatBitSize, HasAnyUndefs,
                             8, true) &&
        SplatBitSize <= 64) {
      // First try assuming that any undefined bits above the highest set bit
      // and below the lowest set bit are 1s.  This increases the likelihood of
      // being able to use a sign-extended element value in VECTOR REPLICATE
      // IMMEDIATE or a wraparound mask in VECTOR GENERATE MASK.
      uint64_t SplatBitsZ = SplatBits.getZExtValue();
      uint64_t SplatUndefZ = SplatUndef.getZExtValue();
      uint64_t Lower = (SplatUndefZ
                        & ((uint64_t(1) << findFirstSet(SplatBitsZ)) - 1));
      uint64_t Upper = (SplatUndefZ
                        & ~((uint64_t(1) << findLastSet(SplatBitsZ)) - 1));
      uint64_t Value = SplatBitsZ | Upper | Lower;
      SDValue Op = tryBuildVectorReplicate(DAG, TII, DL, VT, Value,
                                           SplatBitSize);
      if (Op.getNode())
        return Op;

      // Now try assuming that any undefined bits between the first and
      // last defined set bits are set.  This increases the chances of
      // using a non-wraparound mask.
      uint64_t Middle = SplatUndefZ & ~Upper & ~Lower;
      Value = SplatBitsZ | Middle;
      Op = tryBuildVectorReplicate(DAG, TII, DL, VT, Value, SplatBitSize);
      if (Op.getNode())
        return Op;
    }

    // Fall back to loading it from memory.
    return SDValue();
  }

  // See if we should use shuffles to construct the vector from other vectors.
  SDValue Res = tryBuildVectorShuffle(DAG, BVN);
  if (Res.getNode())
    return Res;

  // Detect SCALAR_TO_VECTOR conversions.
  if (isOperationLegal(ISD::SCALAR_TO_VECTOR, VT) && isScalarToVector(Op))
    return buildScalarToVector(DAG, DL, VT, Op.getOperand(0));

  // Otherwise use buildVector to build the vector up from GPRs.
  unsigned NumElements = Op.getNumOperands();
  SmallVector<SDValue, SystemZ::VectorBytes> Ops(NumElements);
  for (unsigned I = 0; I < NumElements; ++I)
    Ops[I] = Op.getOperand(I);
  return buildVector(DAG, DL, VT, Ops);
}

SDValue SystemZTargetLowering::lowerVECTOR_SHUFFLE(SDValue Op,
                                                   SelectionDAG &DAG) const {
  auto *VSN = cast<ShuffleVectorSDNode>(Op.getNode());
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  unsigned NumElements = VT.getVectorNumElements();

  if (VSN->isSplat()) {
    SDValue Op0 = Op.getOperand(0);
    unsigned Index = VSN->getSplatIndex();
    assert(Index < VT.getVectorNumElements() &&
           "Splat index should be defined and in first operand");
    // See whether the value we're splatting is directly available as a scalar.
    if ((Index == 0 && Op0.getOpcode() == ISD::SCALAR_TO_VECTOR) ||
        Op0.getOpcode() == ISD::BUILD_VECTOR)
      return DAG.getNode(SystemZISD::REPLICATE, DL, VT, Op0.getOperand(Index));
    // Otherwise keep it as a vector-to-vector operation.
    return DAG.getNode(SystemZISD::SPLAT, DL, VT, Op.getOperand(0),
                       DAG.getConstant(Index, DL, MVT::i32));
  }

  GeneralShuffle GS(VT);
  for (unsigned I = 0; I < NumElements; ++I) {
    int Elt = VSN->getMaskElt(I);
    if (Elt < 0)
      GS.addUndef();
    else
      GS.add(Op.getOperand(unsigned(Elt) / NumElements),
             unsigned(Elt) % NumElements);
  }
  return GS.getNode(DAG, SDLoc(VSN));
}

SDValue SystemZTargetLowering::lowerSCALAR_TO_VECTOR(SDValue Op,
                                                     SelectionDAG &DAG) const {
  SDLoc DL(Op);
  // Just insert the scalar into element 0 of an undefined vector.
  return DAG.getNode(ISD::INSERT_VECTOR_ELT, DL,
                     Op.getValueType(), DAG.getUNDEF(Op.getValueType()),
                     Op.getOperand(0), DAG.getConstant(0, DL, MVT::i32));
}

SDValue SystemZTargetLowering::lowerINSERT_VECTOR_ELT(SDValue Op,
                                                      SelectionDAG &DAG) const {
  // Handle insertions of floating-point values.
  SDLoc DL(Op);
  SDValue Op0 = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  SDValue Op2 = Op.getOperand(2);
  EVT VT = Op.getValueType();

  // Insertions into constant indices of a v2f64 can be done using VPDI.
  // However, if the inserted value is a bitcast or a constant then it's
  // better to use GPRs, as below.
  if (VT == MVT::v2f64 &&
      Op1.getOpcode() != ISD::BITCAST &&
      Op1.getOpcode() != ISD::ConstantFP &&
      Op2.getOpcode() == ISD::Constant) {
    uint64_t Index = dyn_cast<ConstantSDNode>(Op2)->getZExtValue();
    unsigned Mask = VT.getVectorNumElements() - 1;
    if (Index <= Mask)
      return Op;
  }

  // Otherwise bitcast to the equivalent integer form and insert via a GPR.
  MVT IntVT = MVT::getIntegerVT(VT.getVectorElementType().getSizeInBits());
  MVT IntVecVT = MVT::getVectorVT(IntVT, VT.getVectorNumElements());
  SDValue Res = DAG.getNode(ISD::INSERT_VECTOR_ELT, DL, IntVecVT,
                            DAG.getNode(ISD::BITCAST, DL, IntVecVT, Op0),
                            DAG.getNode(ISD::BITCAST, DL, IntVT, Op1), Op2);
  return DAG.getNode(ISD::BITCAST, DL, VT, Res);
}

SDValue
SystemZTargetLowering::lowerEXTRACT_VECTOR_ELT(SDValue Op,
                                               SelectionDAG &DAG) const {
  // Handle extractions of floating-point values.
  SDLoc DL(Op);
  SDValue Op0 = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  EVT VT = Op.getValueType();
  EVT VecVT = Op0.getValueType();

  // Extractions of constant indices can be done directly.
  if (auto *CIndexN = dyn_cast<ConstantSDNode>(Op1)) {
    uint64_t Index = CIndexN->getZExtValue();
    unsigned Mask = VecVT.getVectorNumElements() - 1;
    if (Index <= Mask)
      return Op;
  }

  // Otherwise bitcast to the equivalent integer form and extract via a GPR.
  MVT IntVT = MVT::getIntegerVT(VT.getSizeInBits());
  MVT IntVecVT = MVT::getVectorVT(IntVT, VecVT.getVectorNumElements());
  SDValue Res = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, IntVT,
                            DAG.getNode(ISD::BITCAST, DL, IntVecVT, Op0), Op1);
  return DAG.getNode(ISD::BITCAST, DL, VT, Res);
}

SDValue
SystemZTargetLowering::lowerExtendVectorInreg(SDValue Op, SelectionDAG &DAG,
                                              unsigned UnpackHigh) const {
  SDValue PackedOp = Op.getOperand(0);
  EVT OutVT = Op.getValueType();
  EVT InVT = PackedOp.getValueType();
  unsigned ToBits = OutVT.getVectorElementType().getSizeInBits();
  unsigned FromBits = InVT.getVectorElementType().getSizeInBits();
  do {
    FromBits *= 2;
    EVT OutVT = MVT::getVectorVT(MVT::getIntegerVT(FromBits),
                                 SystemZ::VectorBits / FromBits);
    PackedOp = DAG.getNode(UnpackHigh, SDLoc(PackedOp), OutVT, PackedOp);
  } while (FromBits != ToBits);
  return PackedOp;
}

SDValue SystemZTargetLowering::lowerShift(SDValue Op, SelectionDAG &DAG,
                                          unsigned ByScalar) const {
  // Look for cases where a vector shift can use the *_BY_SCALAR form.
  SDValue Op0 = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  unsigned ElemBitSize = VT.getVectorElementType().getSizeInBits();

  // See whether the shift vector is a splat represented as BUILD_VECTOR.
  if (auto *BVN = dyn_cast<BuildVectorSDNode>(Op1)) {
    APInt SplatBits, SplatUndef;
    unsigned SplatBitSize;
    bool HasAnyUndefs;
    // Check for constant splats.  Use ElemBitSize as the minimum element
    // width and reject splats that need wider elements.
    if (BVN->isConstantSplat(SplatBits, SplatUndef, SplatBitSize, HasAnyUndefs,
                             ElemBitSize, true) &&
        SplatBitSize == ElemBitSize) {
      SDValue Shift = DAG.getConstant(SplatBits.getZExtValue() & 0xfff,
                                      DL, MVT::i32);
      return DAG.getNode(ByScalar, DL, VT, Op0, Shift);
    }
    // Check for variable splats.
    BitVector UndefElements;
    SDValue Splat = BVN->getSplatValue(&UndefElements);
    if (Splat) {
      // Since i32 is the smallest legal type, we either need a no-op
      // or a truncation.
      SDValue Shift = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Splat);
      return DAG.getNode(ByScalar, DL, VT, Op0, Shift);
    }
  }

  // See whether the shift vector is a splat represented as SHUFFLE_VECTOR,
  // and the shift amount is directly available in a GPR.
  if (auto *VSN = dyn_cast<ShuffleVectorSDNode>(Op1)) {
    if (VSN->isSplat()) {
      SDValue VSNOp0 = VSN->getOperand(0);
      unsigned Index = VSN->getSplatIndex();
      assert(Index < VT.getVectorNumElements() &&
             "Splat index should be defined and in first operand");
      if ((Index == 0 && VSNOp0.getOpcode() == ISD::SCALAR_TO_VECTOR) ||
          VSNOp0.getOpcode() == ISD::BUILD_VECTOR) {
        // Since i32 is the smallest legal type, we either need a no-op
        // or a truncation.
        SDValue Shift = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32,
                                    VSNOp0.getOperand(Index));
        return DAG.getNode(ByScalar, DL, VT, Op0, Shift);
      }
    }
  }

  // Otherwise just treat the current form as legal.
  return Op;
}

SDValue SystemZTargetLowering::LowerOperation(SDValue Op,
                                              SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::BR_CC:
    return lowerBR_CC(Op, DAG);
  case ISD::SELECT_CC:
    return lowerSELECT_CC(Op, DAG);
  case ISD::SETCC:
    return lowerSETCC(Op, DAG);
  case ISD::GlobalAddress:
    return lowerGlobalAddress(cast<GlobalAddressSDNode>(Op), DAG);
  case ISD::GlobalTLSAddress:
    return lowerGlobalTLSAddress(cast<GlobalAddressSDNode>(Op), DAG);
  case ISD::BlockAddress:
    return lowerBlockAddress(cast<BlockAddressSDNode>(Op), DAG);
  case ISD::JumpTable:
    return lowerJumpTable(cast<JumpTableSDNode>(Op), DAG);
  case ISD::ConstantPool:
    return lowerConstantPool(cast<ConstantPoolSDNode>(Op), DAG);
  case ISD::BITCAST:
    return lowerBITCAST(Op, DAG);
  case ISD::VASTART:
    return lowerVASTART(Op, DAG);
  case ISD::VACOPY:
    return lowerVACOPY(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    return lowerDYNAMIC_STACKALLOC(Op, DAG);
  case ISD::SMUL_LOHI:
    return lowerSMUL_LOHI(Op, DAG);
  case ISD::UMUL_LOHI:
    return lowerUMUL_LOHI(Op, DAG);
  case ISD::SDIVREM:
    return lowerSDIVREM(Op, DAG);
  case ISD::UDIVREM:
    return lowerUDIVREM(Op, DAG);
  case ISD::OR:
    return lowerOR(Op, DAG);
  case ISD::CTPOP:
    return lowerCTPOP(Op, DAG);
  case ISD::CTLZ_ZERO_UNDEF:
    return DAG.getNode(ISD::CTLZ, SDLoc(Op),
                       Op.getValueType(), Op.getOperand(0));
  case ISD::CTTZ_ZERO_UNDEF:
    return DAG.getNode(ISD::CTTZ, SDLoc(Op),
                       Op.getValueType(), Op.getOperand(0));
  case ISD::ATOMIC_SWAP:
    return lowerATOMIC_LOAD_OP(Op, DAG, SystemZISD::ATOMIC_SWAPW);
  case ISD::ATOMIC_STORE:
    return lowerATOMIC_STORE(Op, DAG);
  case ISD::ATOMIC_LOAD:
    return lowerATOMIC_LOAD(Op, DAG);
  case ISD::ATOMIC_LOAD_ADD:
    return lowerATOMIC_LOAD_OP(Op, DAG, SystemZISD::ATOMIC_LOADW_ADD);
  case ISD::ATOMIC_LOAD_SUB:
    return lowerATOMIC_LOAD_SUB(Op, DAG);
  case ISD::ATOMIC_LOAD_AND:
    return lowerATOMIC_LOAD_OP(Op, DAG, SystemZISD::ATOMIC_LOADW_AND);
  case ISD::ATOMIC_LOAD_OR:
    return lowerATOMIC_LOAD_OP(Op, DAG, SystemZISD::ATOMIC_LOADW_OR);
  case ISD::ATOMIC_LOAD_XOR:
    return lowerATOMIC_LOAD_OP(Op, DAG, SystemZISD::ATOMIC_LOADW_XOR);
  case ISD::ATOMIC_LOAD_NAND:
    return lowerATOMIC_LOAD_OP(Op, DAG, SystemZISD::ATOMIC_LOADW_NAND);
  case ISD::ATOMIC_LOAD_MIN:
    return lowerATOMIC_LOAD_OP(Op, DAG, SystemZISD::ATOMIC_LOADW_MIN);
  case ISD::ATOMIC_LOAD_MAX:
    return lowerATOMIC_LOAD_OP(Op, DAG, SystemZISD::ATOMIC_LOADW_MAX);
  case ISD::ATOMIC_LOAD_UMIN:
    return lowerATOMIC_LOAD_OP(Op, DAG, SystemZISD::ATOMIC_LOADW_UMIN);
  case ISD::ATOMIC_LOAD_UMAX:
    return lowerATOMIC_LOAD_OP(Op, DAG, SystemZISD::ATOMIC_LOADW_UMAX);
  case ISD::ATOMIC_CMP_SWAP:
    return lowerATOMIC_CMP_SWAP(Op, DAG);
  case ISD::STACKSAVE:
    return lowerSTACKSAVE(Op, DAG);
  case ISD::STACKRESTORE:
    return lowerSTACKRESTORE(Op, DAG);
  case ISD::PREFETCH:
    return lowerPREFETCH(Op, DAG);
  case ISD::INTRINSIC_W_CHAIN:
    return lowerINTRINSIC_W_CHAIN(Op, DAG);
  case ISD::INTRINSIC_WO_CHAIN:
    return lowerINTRINSIC_WO_CHAIN(Op, DAG);
  case ISD::BUILD_VECTOR:
    return lowerBUILD_VECTOR(Op, DAG);
  case ISD::VECTOR_SHUFFLE:
    return lowerVECTOR_SHUFFLE(Op, DAG);
  case ISD::SCALAR_TO_VECTOR:
    return lowerSCALAR_TO_VECTOR(Op, DAG);
  case ISD::INSERT_VECTOR_ELT:
    return lowerINSERT_VECTOR_ELT(Op, DAG);
  case ISD::EXTRACT_VECTOR_ELT:
    return lowerEXTRACT_VECTOR_ELT(Op, DAG);
  case ISD::SIGN_EXTEND_VECTOR_INREG:
    return lowerExtendVectorInreg(Op, DAG, SystemZISD::UNPACK_HIGH);
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    return lowerExtendVectorInreg(Op, DAG, SystemZISD::UNPACKL_HIGH);
  case ISD::SHL:
    return lowerShift(Op, DAG, SystemZISD::VSHL_BY_SCALAR);
  case ISD::SRL:
    return lowerShift(Op, DAG, SystemZISD::VSRL_BY_SCALAR);
  case ISD::SRA:
    return lowerShift(Op, DAG, SystemZISD::VSRA_BY_SCALAR);
  default:
    llvm_unreachable("Unexpected node to lower");
  }
}

const char *SystemZTargetLowering::getTargetNodeName(unsigned Opcode) const {
#define OPCODE(NAME) case SystemZISD::NAME: return "SystemZISD::" #NAME
  switch ((SystemZISD::NodeType)Opcode) {
    case SystemZISD::FIRST_NUMBER: break;
    OPCODE(RET_FLAG);
    OPCODE(CALL);
    OPCODE(SIBCALL);
    OPCODE(TLS_GDCALL);
    OPCODE(TLS_LDCALL);
    OPCODE(PCREL_WRAPPER);
    OPCODE(PCREL_OFFSET);
    OPCODE(IABS);
    OPCODE(ICMP);
    OPCODE(FCMP);
    OPCODE(TM);
    OPCODE(BR_CCMASK);
    OPCODE(SELECT_CCMASK);
    OPCODE(ADJDYNALLOC);
    OPCODE(EXTRACT_ACCESS);
    OPCODE(POPCNT);
    OPCODE(UMUL_LOHI64);
    OPCODE(SDIVREM32);
    OPCODE(SDIVREM64);
    OPCODE(UDIVREM32);
    OPCODE(UDIVREM64);
    OPCODE(MVC);
    OPCODE(MVC_LOOP);
    OPCODE(NC);
    OPCODE(NC_LOOP);
    OPCODE(OC);
    OPCODE(OC_LOOP);
    OPCODE(XC);
    OPCODE(XC_LOOP);
    OPCODE(CLC);
    OPCODE(CLC_LOOP);
    OPCODE(STPCPY);
    OPCODE(STRCMP);
    OPCODE(SEARCH_STRING);
    OPCODE(IPM);
    OPCODE(SERIALIZE);
    OPCODE(TBEGIN);
    OPCODE(TBEGIN_NOFLOAT);
    OPCODE(TEND);
    OPCODE(BYTE_MASK);
    OPCODE(ROTATE_MASK);
    OPCODE(REPLICATE);
    OPCODE(JOIN_DWORDS);
    OPCODE(SPLAT);
    OPCODE(MERGE_HIGH);
    OPCODE(MERGE_LOW);
    OPCODE(SHL_DOUBLE);
    OPCODE(PERMUTE_DWORDS);
    OPCODE(PERMUTE);
    OPCODE(PACK);
    OPCODE(PACKS_CC);
    OPCODE(PACKLS_CC);
    OPCODE(UNPACK_HIGH);
    OPCODE(UNPACKL_HIGH);
    OPCODE(UNPACK_LOW);
    OPCODE(UNPACKL_LOW);
    OPCODE(VSHL_BY_SCALAR);
    OPCODE(VSRL_BY_SCALAR);
    OPCODE(VSRA_BY_SCALAR);
    OPCODE(VSUM);
    OPCODE(VICMPE);
    OPCODE(VICMPH);
    OPCODE(VICMPHL);
    OPCODE(VICMPES);
    OPCODE(VICMPHS);
    OPCODE(VICMPHLS);
    OPCODE(VFCMPE);
    OPCODE(VFCMPH);
    OPCODE(VFCMPHE);
    OPCODE(VFCMPES);
    OPCODE(VFCMPHS);
    OPCODE(VFCMPHES);
    OPCODE(VFTCI);
    OPCODE(VEXTEND);
    OPCODE(VROUND);
    OPCODE(VTM);
    OPCODE(VFAE_CC);
    OPCODE(VFAEZ_CC);
    OPCODE(VFEE_CC);
    OPCODE(VFEEZ_CC);
    OPCODE(VFENE_CC);
    OPCODE(VFENEZ_CC);
    OPCODE(VISTR_CC);
    OPCODE(VSTRC_CC);
    OPCODE(VSTRCZ_CC);
    OPCODE(ATOMIC_SWAPW);
    OPCODE(ATOMIC_LOADW_ADD);
    OPCODE(ATOMIC_LOADW_SUB);
    OPCODE(ATOMIC_LOADW_AND);
    OPCODE(ATOMIC_LOADW_OR);
    OPCODE(ATOMIC_LOADW_XOR);
    OPCODE(ATOMIC_LOADW_NAND);
    OPCODE(ATOMIC_LOADW_MIN);
    OPCODE(ATOMIC_LOADW_MAX);
    OPCODE(ATOMIC_LOADW_UMIN);
    OPCODE(ATOMIC_LOADW_UMAX);
    OPCODE(ATOMIC_CMP_SWAPW);
    OPCODE(PREFETCH);
  }
  return nullptr;
#undef OPCODE
}

// Return true if VT is a vector whose elements are a whole number of bytes
// in width.
static bool canTreatAsByteVector(EVT VT) {
  return VT.isVector() && VT.getVectorElementType().getSizeInBits() % 8 == 0;
}

// Try to simplify an EXTRACT_VECTOR_ELT from a vector of type VecVT
// producing a result of type ResVT.  Op is a possibly bitcast version
// of the input vector and Index is the index (based on type VecVT) that
// should be extracted.  Return the new extraction if a simplification
// was possible or if Force is true.
SDValue SystemZTargetLowering::combineExtract(SDLoc DL, EVT ResVT, EVT VecVT,
                                              SDValue Op, unsigned Index,
                                              DAGCombinerInfo &DCI,
                                              bool Force) const {
  SelectionDAG &DAG = DCI.DAG;

  // The number of bytes being extracted.
  unsigned BytesPerElement = VecVT.getVectorElementType().getStoreSize();

  for (;;) {
    unsigned Opcode = Op.getOpcode();
    if (Opcode == ISD::BITCAST)
      // Look through bitcasts.
      Op = Op.getOperand(0);
    else if (Opcode == ISD::VECTOR_SHUFFLE &&
             canTreatAsByteVector(Op.getValueType())) {
      // Get a VPERM-like permute mask and see whether the bytes covered
      // by the extracted element are a contiguous sequence from one
      // source operand.
      SmallVector<int, SystemZ::VectorBytes> Bytes;
      getVPermMask(cast<ShuffleVectorSDNode>(Op), Bytes);
      int First;
      if (!getShuffleInput(Bytes, Index * BytesPerElement,
                           BytesPerElement, First))
        break;
      if (First < 0)
        return DAG.getUNDEF(ResVT);
      // Make sure the contiguous sequence starts at a multiple of the
      // original element size.
      unsigned Byte = unsigned(First) % Bytes.size();
      if (Byte % BytesPerElement != 0)
        break;
      // We can get the extracted value directly from an input.
      Index = Byte / BytesPerElement;
      Op = Op.getOperand(unsigned(First) / Bytes.size());
      Force = true;
    } else if (Opcode == ISD::BUILD_VECTOR &&
               canTreatAsByteVector(Op.getValueType())) {
      // We can only optimize this case if the BUILD_VECTOR elements are
      // at least as wide as the extracted value.
      EVT OpVT = Op.getValueType();
      unsigned OpBytesPerElement = OpVT.getVectorElementType().getStoreSize();
      if (OpBytesPerElement < BytesPerElement)
        break;
      // Make sure that the least-significant bit of the extracted value
      // is the least significant bit of an input.
      unsigned End = (Index + 1) * BytesPerElement;
      if (End % OpBytesPerElement != 0)
        break;
      // We're extracting the low part of one operand of the BUILD_VECTOR.
      Op = Op.getOperand(End / OpBytesPerElement - 1);
      if (!Op.getValueType().isInteger()) {
        EVT VT = MVT::getIntegerVT(Op.getValueType().getSizeInBits());
        Op = DAG.getNode(ISD::BITCAST, DL, VT, Op);
        DCI.AddToWorklist(Op.getNode());
      }
      EVT VT = MVT::getIntegerVT(ResVT.getSizeInBits());
      Op = DAG.getNode(ISD::TRUNCATE, DL, VT, Op);
      if (VT != ResVT) {
        DCI.AddToWorklist(Op.getNode());
        Op = DAG.getNode(ISD::BITCAST, DL, ResVT, Op);
      }
      return Op;
    } else if ((Opcode == ISD::SIGN_EXTEND_VECTOR_INREG ||
                Opcode == ISD::ZERO_EXTEND_VECTOR_INREG ||
                Opcode == ISD::ANY_EXTEND_VECTOR_INREG) &&
               canTreatAsByteVector(Op.getValueType()) &&
               canTreatAsByteVector(Op.getOperand(0).getValueType())) {
      // Make sure that only the unextended bits are significant.
      EVT ExtVT = Op.getValueType();
      EVT OpVT = Op.getOperand(0).getValueType();
      unsigned ExtBytesPerElement = ExtVT.getVectorElementType().getStoreSize();
      unsigned OpBytesPerElement = OpVT.getVectorElementType().getStoreSize();
      unsigned Byte = Index * BytesPerElement;
      unsigned SubByte = Byte % ExtBytesPerElement;
      unsigned MinSubByte = ExtBytesPerElement - OpBytesPerElement;
      if (SubByte < MinSubByte ||
          SubByte + BytesPerElement > ExtBytesPerElement)
        break;
      // Get the byte offset of the unextended element
      Byte = Byte / ExtBytesPerElement * OpBytesPerElement;
      // ...then add the byte offset relative to that element.
      Byte += SubByte - MinSubByte;
      if (Byte % BytesPerElement != 0)
        break;
      Op = Op.getOperand(0);
      Index = Byte / BytesPerElement;
      Force = true;
    } else
      break;
  }
  if (Force) {
    if (Op.getValueType() != VecVT) {
      Op = DAG.getNode(ISD::BITCAST, DL, VecVT, Op);
      DCI.AddToWorklist(Op.getNode());
    }
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, ResVT, Op,
                       DAG.getConstant(Index, DL, MVT::i32));
  }
  return SDValue();
}

// Optimize vector operations in scalar value Op on the basis that Op
// is truncated to TruncVT.
SDValue
SystemZTargetLowering::combineTruncateExtract(SDLoc DL, EVT TruncVT, SDValue Op,
                                              DAGCombinerInfo &DCI) const {
  // If we have (trunc (extract_vector_elt X, Y)), try to turn it into
  // (extract_vector_elt (bitcast X), Y'), where (bitcast X) has elements
  // of type TruncVT.
  if (Op.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
      TruncVT.getSizeInBits() % 8 == 0) {
    SDValue Vec = Op.getOperand(0);
    EVT VecVT = Vec.getValueType();
    if (canTreatAsByteVector(VecVT)) {
      if (auto *IndexN = dyn_cast<ConstantSDNode>(Op.getOperand(1))) {
        unsigned BytesPerElement = VecVT.getVectorElementType().getStoreSize();
        unsigned TruncBytes = TruncVT.getStoreSize();
        if (BytesPerElement % TruncBytes == 0) {
          // Calculate the value of Y' in the above description.  We are
          // splitting the original elements into Scale equal-sized pieces
          // and for truncation purposes want the last (least-significant)
          // of these pieces for IndexN.  This is easiest to do by calculating
          // the start index of the following element and then subtracting 1.
          unsigned Scale = BytesPerElement / TruncBytes;
          unsigned NewIndex = (IndexN->getZExtValue() + 1) * Scale - 1;

          // Defer the creation of the bitcast from X to combineExtract,
          // which might be able to optimize the extraction.
          VecVT = MVT::getVectorVT(MVT::getIntegerVT(TruncBytes * 8),
                                   VecVT.getStoreSize() / TruncBytes);
          EVT ResVT = (TruncBytes < 4 ? MVT::i32 : TruncVT);
          return combineExtract(DL, ResVT, VecVT, Vec, NewIndex, DCI, true);
        }
      }
    }
  }
  return SDValue();
}

SDValue SystemZTargetLowering::PerformDAGCombine(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  unsigned Opcode = N->getOpcode();
  if (Opcode == ISD::SIGN_EXTEND) {
    // Convert (sext (ashr (shl X, C1), C2)) to
    // (ashr (shl (anyext X), C1'), C2')), since wider shifts are as
    // cheap as narrower ones.
    SDValue N0 = N->getOperand(0);
    EVT VT = N->getValueType(0);
    if (N0.hasOneUse() && N0.getOpcode() == ISD::SRA) {
      auto *SraAmt = dyn_cast<ConstantSDNode>(N0.getOperand(1));
      SDValue Inner = N0.getOperand(0);
      if (SraAmt && Inner.hasOneUse() && Inner.getOpcode() == ISD::SHL) {
        if (auto *ShlAmt = dyn_cast<ConstantSDNode>(Inner.getOperand(1))) {
          unsigned Extra = (VT.getSizeInBits() -
                            N0.getValueType().getSizeInBits());
          unsigned NewShlAmt = ShlAmt->getZExtValue() + Extra;
          unsigned NewSraAmt = SraAmt->getZExtValue() + Extra;
          EVT ShiftVT = N0.getOperand(1).getValueType();
          SDValue Ext = DAG.getNode(ISD::ANY_EXTEND, SDLoc(Inner), VT,
                                    Inner.getOperand(0));
          SDValue Shl = DAG.getNode(ISD::SHL, SDLoc(Inner), VT, Ext,
                                    DAG.getConstant(NewShlAmt, SDLoc(Inner),
                                                    ShiftVT));
          return DAG.getNode(ISD::SRA, SDLoc(N0), VT, Shl,
                             DAG.getConstant(NewSraAmt, SDLoc(N0), ShiftVT));
        }
      }
    }
  }
  if (Opcode == SystemZISD::MERGE_HIGH ||
      Opcode == SystemZISD::MERGE_LOW) {
    SDValue Op0 = N->getOperand(0);
    SDValue Op1 = N->getOperand(1);
    if (Op0.getOpcode() == ISD::BITCAST)
      Op0 = Op0.getOperand(0);
    if (Op0.getOpcode() == SystemZISD::BYTE_MASK &&
        cast<ConstantSDNode>(Op0.getOperand(0))->getZExtValue() == 0) {
      // (z_merge_* 0, 0) -> 0.  This is mostly useful for using VLLEZF
      // for v4f32.
      if (Op1 == N->getOperand(0))
        return Op1;
      // (z_merge_? 0, X) -> (z_unpackl_? 0, X).
      EVT VT = Op1.getValueType();
      unsigned ElemBytes = VT.getVectorElementType().getStoreSize();
      if (ElemBytes <= 4) {
        Opcode = (Opcode == SystemZISD::MERGE_HIGH ?
                  SystemZISD::UNPACKL_HIGH : SystemZISD::UNPACKL_LOW);
        EVT InVT = VT.changeVectorElementTypeToInteger();
        EVT OutVT = MVT::getVectorVT(MVT::getIntegerVT(ElemBytes * 16),
                                     SystemZ::VectorBytes / ElemBytes / 2);
        if (VT != InVT) {
          Op1 = DAG.getNode(ISD::BITCAST, SDLoc(N), InVT, Op1);
          DCI.AddToWorklist(Op1.getNode());
        }
        SDValue Op = DAG.getNode(Opcode, SDLoc(N), OutVT, Op1);
        DCI.AddToWorklist(Op.getNode());
        return DAG.getNode(ISD::BITCAST, SDLoc(N), VT, Op);
      }
    }
  }
  // If we have (truncstoreiN (extract_vector_elt X, Y), Z) then it is better
  // for the extraction to be done on a vMiN value, so that we can use VSTE.
  // If X has wider elements then convert it to:
  // (truncstoreiN (extract_vector_elt (bitcast X), Y2), Z).
  if (Opcode == ISD::STORE) {
    auto *SN = cast<StoreSDNode>(N);
    EVT MemVT = SN->getMemoryVT();
    if (MemVT.isInteger()) {
      SDValue Value = combineTruncateExtract(SDLoc(N), MemVT,
                                             SN->getValue(), DCI);
      if (Value.getNode()) {
        DCI.AddToWorklist(Value.getNode());

        // Rewrite the store with the new form of stored value.
        return DAG.getTruncStore(SN->getChain(), SDLoc(SN), Value,
                                 SN->getBasePtr(), SN->getMemoryVT(),
                                 SN->getMemOperand());
      }
    }
  }
  // Try to simplify a vector extraction.
  if (Opcode == ISD::EXTRACT_VECTOR_ELT) {
    if (auto *IndexN = dyn_cast<ConstantSDNode>(N->getOperand(1))) {
      SDValue Op0 = N->getOperand(0);
      EVT VecVT = Op0.getValueType();
      return combineExtract(SDLoc(N), N->getValueType(0), VecVT, Op0,
                            IndexN->getZExtValue(), DCI, false);
    }
  }
  // (join_dwords X, X) == (replicate X)
  if (Opcode == SystemZISD::JOIN_DWORDS &&
      N->getOperand(0) == N->getOperand(1))
    return DAG.getNode(SystemZISD::REPLICATE, SDLoc(N), N->getValueType(0),
                       N->getOperand(0));
  // (fround (extract_vector_elt X 0))
  // (fround (extract_vector_elt X 1)) ->
  // (extract_vector_elt (VROUND X) 0)
  // (extract_vector_elt (VROUND X) 1)
  //
  // This is a special case since the target doesn't really support v2f32s.
  if (Opcode == ISD::FP_ROUND) {
    SDValue Op0 = N->getOperand(0);
    if (N->getValueType(0) == MVT::f32 &&
        Op0.hasOneUse() &&
        Op0.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
        Op0.getOperand(0).getValueType() == MVT::v2f64 &&
        Op0.getOperand(1).getOpcode() == ISD::Constant &&
        cast<ConstantSDNode>(Op0.getOperand(1))->getZExtValue() == 0) {
      SDValue Vec = Op0.getOperand(0);
      for (auto *U : Vec->uses()) {
        if (U != Op0.getNode() &&
            U->hasOneUse() &&
            U->getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
            U->getOperand(0) == Vec &&
            U->getOperand(1).getOpcode() == ISD::Constant &&
            cast<ConstantSDNode>(U->getOperand(1))->getZExtValue() == 1) {
          SDValue OtherRound = SDValue(*U->use_begin(), 0);
          if (OtherRound.getOpcode() == ISD::FP_ROUND &&
              OtherRound.getOperand(0) == SDValue(U, 0) &&
              OtherRound.getValueType() == MVT::f32) {
            SDValue VRound = DAG.getNode(SystemZISD::VROUND, SDLoc(N),
                                         MVT::v4f32, Vec);
            DCI.AddToWorklist(VRound.getNode());
            SDValue Extract1 =
              DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SDLoc(U), MVT::f32,
                          VRound, DAG.getConstant(2, SDLoc(U), MVT::i32));
            DCI.AddToWorklist(Extract1.getNode());
            DAG.ReplaceAllUsesOfValueWith(OtherRound, Extract1);
            SDValue Extract0 =
              DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SDLoc(Op0), MVT::f32,
                          VRound, DAG.getConstant(0, SDLoc(Op0), MVT::i32));
            return Extract0;
          }
        }
      }
    }
  }
  return SDValue();
}

//===----------------------------------------------------------------------===//
// Custom insertion
//===----------------------------------------------------------------------===//

// Create a new basic block after MBB.
static MachineBasicBlock *emitBlockAfter(MachineBasicBlock *MBB) {
  MachineFunction &MF = *MBB->getParent();
  MachineBasicBlock *NewMBB = MF.CreateMachineBasicBlock(MBB->getBasicBlock());
  MF.insert(std::next(MachineFunction::iterator(MBB)), NewMBB);
  return NewMBB;
}

// Split MBB after MI and return the new block (the one that contains
// instructions after MI).
static MachineBasicBlock *splitBlockAfter(MachineInstr *MI,
                                          MachineBasicBlock *MBB) {
  MachineBasicBlock *NewMBB = emitBlockAfter(MBB);
  NewMBB->splice(NewMBB->begin(), MBB,
                 std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  NewMBB->transferSuccessorsAndUpdatePHIs(MBB);
  return NewMBB;
}

// Split MBB before MI and return the new block (the one that contains MI).
static MachineBasicBlock *splitBlockBefore(MachineInstr *MI,
                                           MachineBasicBlock *MBB) {
  MachineBasicBlock *NewMBB = emitBlockAfter(MBB);
  NewMBB->splice(NewMBB->begin(), MBB, MI, MBB->end());
  NewMBB->transferSuccessorsAndUpdatePHIs(MBB);
  return NewMBB;
}

// Force base value Base into a register before MI.  Return the register.
static unsigned forceReg(MachineInstr *MI, MachineOperand &Base,
                         const SystemZInstrInfo *TII) {
  if (Base.isReg())
    return Base.getReg();

  MachineBasicBlock *MBB = MI->getParent();
  MachineFunction &MF = *MBB->getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  unsigned Reg = MRI.createVirtualRegister(&SystemZ::ADDR64BitRegClass);
  BuildMI(*MBB, MI, MI->getDebugLoc(), TII->get(SystemZ::LA), Reg)
    .addOperand(Base).addImm(0).addReg(0);
  return Reg;
}

// Implement EmitInstrWithCustomInserter for pseudo Select* instruction MI.
MachineBasicBlock *
SystemZTargetLowering::emitSelect(MachineInstr *MI,
                                  MachineBasicBlock *MBB) const {
  const SystemZInstrInfo *TII =
      static_cast<const SystemZInstrInfo *>(Subtarget.getInstrInfo());

  unsigned DestReg  = MI->getOperand(0).getReg();
  unsigned TrueReg  = MI->getOperand(1).getReg();
  unsigned FalseReg = MI->getOperand(2).getReg();
  unsigned CCValid  = MI->getOperand(3).getImm();
  unsigned CCMask   = MI->getOperand(4).getImm();
  DebugLoc DL       = MI->getDebugLoc();

  MachineBasicBlock *StartMBB = MBB;
  MachineBasicBlock *JoinMBB  = splitBlockBefore(MI, MBB);
  MachineBasicBlock *FalseMBB = emitBlockAfter(StartMBB);

  //  StartMBB:
  //   BRC CCMask, JoinMBB
  //   # fallthrough to FalseMBB
  MBB = StartMBB;
  BuildMI(MBB, DL, TII->get(SystemZ::BRC))
    .addImm(CCValid).addImm(CCMask).addMBB(JoinMBB);
  MBB->addSuccessor(JoinMBB);
  MBB->addSuccessor(FalseMBB);

  //  FalseMBB:
  //   # fallthrough to JoinMBB
  MBB = FalseMBB;
  MBB->addSuccessor(JoinMBB);

  //  JoinMBB:
  //   %Result = phi [ %FalseReg, FalseMBB ], [ %TrueReg, StartMBB ]
  //  ...
  MBB = JoinMBB;
  BuildMI(*MBB, MI, DL, TII->get(SystemZ::PHI), DestReg)
    .addReg(TrueReg).addMBB(StartMBB)
    .addReg(FalseReg).addMBB(FalseMBB);

  MI->eraseFromParent();
  return JoinMBB;
}

// Implement EmitInstrWithCustomInserter for pseudo CondStore* instruction MI.
// StoreOpcode is the store to use and Invert says whether the store should
// happen when the condition is false rather than true.  If a STORE ON
// CONDITION is available, STOCOpcode is its opcode, otherwise it is 0.
MachineBasicBlock *
SystemZTargetLowering::emitCondStore(MachineInstr *MI,
                                     MachineBasicBlock *MBB,
                                     unsigned StoreOpcode, unsigned STOCOpcode,
                                     bool Invert) const {
  const SystemZInstrInfo *TII =
      static_cast<const SystemZInstrInfo *>(Subtarget.getInstrInfo());

  unsigned SrcReg     = MI->getOperand(0).getReg();
  MachineOperand Base = MI->getOperand(1);
  int64_t Disp        = MI->getOperand(2).getImm();
  unsigned IndexReg   = MI->getOperand(3).getReg();
  unsigned CCValid    = MI->getOperand(4).getImm();
  unsigned CCMask     = MI->getOperand(5).getImm();
  DebugLoc DL         = MI->getDebugLoc();

  StoreOpcode = TII->getOpcodeForOffset(StoreOpcode, Disp);

  // Use STOCOpcode if possible.  We could use different store patterns in
  // order to avoid matching the index register, but the performance trade-offs
  // might be more complicated in that case.
  if (STOCOpcode && !IndexReg && Subtarget.hasLoadStoreOnCond()) {
    if (Invert)
      CCMask ^= CCValid;
    BuildMI(*MBB, MI, DL, TII->get(STOCOpcode))
      .addReg(SrcReg).addOperand(Base).addImm(Disp)
      .addImm(CCValid).addImm(CCMask);
    MI->eraseFromParent();
    return MBB;
  }

  // Get the condition needed to branch around the store.
  if (!Invert)
    CCMask ^= CCValid;

  MachineBasicBlock *StartMBB = MBB;
  MachineBasicBlock *JoinMBB  = splitBlockBefore(MI, MBB);
  MachineBasicBlock *FalseMBB = emitBlockAfter(StartMBB);

  //  StartMBB:
  //   BRC CCMask, JoinMBB
  //   # fallthrough to FalseMBB
  MBB = StartMBB;
  BuildMI(MBB, DL, TII->get(SystemZ::BRC))
    .addImm(CCValid).addImm(CCMask).addMBB(JoinMBB);
  MBB->addSuccessor(JoinMBB);
  MBB->addSuccessor(FalseMBB);

  //  FalseMBB:
  //   store %SrcReg, %Disp(%Index,%Base)
  //   # fallthrough to JoinMBB
  MBB = FalseMBB;
  BuildMI(MBB, DL, TII->get(StoreOpcode))
    .addReg(SrcReg).addOperand(Base).addImm(Disp).addReg(IndexReg);
  MBB->addSuccessor(JoinMBB);

  MI->eraseFromParent();
  return JoinMBB;
}

// Implement EmitInstrWithCustomInserter for pseudo ATOMIC_LOAD{,W}_*
// or ATOMIC_SWAP{,W} instruction MI.  BinOpcode is the instruction that
// performs the binary operation elided by "*", or 0 for ATOMIC_SWAP{,W}.
// BitSize is the width of the field in bits, or 0 if this is a partword
// ATOMIC_LOADW_* or ATOMIC_SWAPW instruction, in which case the bitsize
// is one of the operands.  Invert says whether the field should be
// inverted after performing BinOpcode (e.g. for NAND).
MachineBasicBlock *
SystemZTargetLowering::emitAtomicLoadBinary(MachineInstr *MI,
                                            MachineBasicBlock *MBB,
                                            unsigned BinOpcode,
                                            unsigned BitSize,
                                            bool Invert) const {
  MachineFunction &MF = *MBB->getParent();
  const SystemZInstrInfo *TII =
      static_cast<const SystemZInstrInfo *>(Subtarget.getInstrInfo());
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool IsSubWord = (BitSize < 32);

  // Extract the operands.  Base can be a register or a frame index.
  // Src2 can be a register or immediate.
  unsigned Dest        = MI->getOperand(0).getReg();
  MachineOperand Base  = earlyUseOperand(MI->getOperand(1));
  int64_t Disp         = MI->getOperand(2).getImm();
  MachineOperand Src2  = earlyUseOperand(MI->getOperand(3));
  unsigned BitShift    = (IsSubWord ? MI->getOperand(4).getReg() : 0);
  unsigned NegBitShift = (IsSubWord ? MI->getOperand(5).getReg() : 0);
  DebugLoc DL          = MI->getDebugLoc();
  if (IsSubWord)
    BitSize = MI->getOperand(6).getImm();

  // Subword operations use 32-bit registers.
  const TargetRegisterClass *RC = (BitSize <= 32 ?
                                   &SystemZ::GR32BitRegClass :
                                   &SystemZ::GR64BitRegClass);
  unsigned LOpcode  = BitSize <= 32 ? SystemZ::L  : SystemZ::LG;
  unsigned CSOpcode = BitSize <= 32 ? SystemZ::CS : SystemZ::CSG;

  // Get the right opcodes for the displacement.
  LOpcode  = TII->getOpcodeForOffset(LOpcode,  Disp);
  CSOpcode = TII->getOpcodeForOffset(CSOpcode, Disp);
  assert(LOpcode && CSOpcode && "Displacement out of range");

  // Create virtual registers for temporary results.
  unsigned OrigVal       = MRI.createVirtualRegister(RC);
  unsigned OldVal        = MRI.createVirtualRegister(RC);
  unsigned NewVal        = (BinOpcode || IsSubWord ?
                            MRI.createVirtualRegister(RC) : Src2.getReg());
  unsigned RotatedOldVal = (IsSubWord ? MRI.createVirtualRegister(RC) : OldVal);
  unsigned RotatedNewVal = (IsSubWord ? MRI.createVirtualRegister(RC) : NewVal);

  // Insert a basic block for the main loop.
  MachineBasicBlock *StartMBB = MBB;
  MachineBasicBlock *DoneMBB  = splitBlockBefore(MI, MBB);
  MachineBasicBlock *LoopMBB  = emitBlockAfter(StartMBB);

  //  StartMBB:
  //   ...
  //   %OrigVal = L Disp(%Base)
  //   # fall through to LoopMMB
  MBB = StartMBB;
  BuildMI(MBB, DL, TII->get(LOpcode), OrigVal)
    .addOperand(Base).addImm(Disp).addReg(0);
  MBB->addSuccessor(LoopMBB);

  //  LoopMBB:
  //   %OldVal        = phi [ %OrigVal, StartMBB ], [ %Dest, LoopMBB ]
  //   %RotatedOldVal = RLL %OldVal, 0(%BitShift)
  //   %RotatedNewVal = OP %RotatedOldVal, %Src2
  //   %NewVal        = RLL %RotatedNewVal, 0(%NegBitShift)
  //   %Dest          = CS %OldVal, %NewVal, Disp(%Base)
  //   JNE LoopMBB
  //   # fall through to DoneMMB
  MBB = LoopMBB;
  BuildMI(MBB, DL, TII->get(SystemZ::PHI), OldVal)
    .addReg(OrigVal).addMBB(StartMBB)
    .addReg(Dest).addMBB(LoopMBB);
  if (IsSubWord)
    BuildMI(MBB, DL, TII->get(SystemZ::RLL), RotatedOldVal)
      .addReg(OldVal).addReg(BitShift).addImm(0);
  if (Invert) {
    // Perform the operation normally and then invert every bit of the field.
    unsigned Tmp = MRI.createVirtualRegister(RC);
    BuildMI(MBB, DL, TII->get(BinOpcode), Tmp)
      .addReg(RotatedOldVal).addOperand(Src2);
    if (BitSize <= 32)
      // XILF with the upper BitSize bits set.
      BuildMI(MBB, DL, TII->get(SystemZ::XILF), RotatedNewVal)
        .addReg(Tmp).addImm(-1U << (32 - BitSize));
    else {
      // Use LCGR and add -1 to the result, which is more compact than
      // an XILF, XILH pair.
      unsigned Tmp2 = MRI.createVirtualRegister(RC);
      BuildMI(MBB, DL, TII->get(SystemZ::LCGR), Tmp2).addReg(Tmp);
      BuildMI(MBB, DL, TII->get(SystemZ::AGHI), RotatedNewVal)
        .addReg(Tmp2).addImm(-1);
    }
  } else if (BinOpcode)
    // A simply binary operation.
    BuildMI(MBB, DL, TII->get(BinOpcode), RotatedNewVal)
      .addReg(RotatedOldVal).addOperand(Src2);
  else if (IsSubWord)
    // Use RISBG to rotate Src2 into position and use it to replace the
    // field in RotatedOldVal.
    BuildMI(MBB, DL, TII->get(SystemZ::RISBG32), RotatedNewVal)
      .addReg(RotatedOldVal).addReg(Src2.getReg())
      .addImm(32).addImm(31 + BitSize).addImm(32 - BitSize);
  if (IsSubWord)
    BuildMI(MBB, DL, TII->get(SystemZ::RLL), NewVal)
      .addReg(RotatedNewVal).addReg(NegBitShift).addImm(0);
  BuildMI(MBB, DL, TII->get(CSOpcode), Dest)
    .addReg(OldVal).addReg(NewVal).addOperand(Base).addImm(Disp);
  BuildMI(MBB, DL, TII->get(SystemZ::BRC))
    .addImm(SystemZ::CCMASK_CS).addImm(SystemZ::CCMASK_CS_NE).addMBB(LoopMBB);
  MBB->addSuccessor(LoopMBB);
  MBB->addSuccessor(DoneMBB);

  MI->eraseFromParent();
  return DoneMBB;
}

// Implement EmitInstrWithCustomInserter for pseudo
// ATOMIC_LOAD{,W}_{,U}{MIN,MAX} instruction MI.  CompareOpcode is the
// instruction that should be used to compare the current field with the
// minimum or maximum value.  KeepOldMask is the BRC condition-code mask
// for when the current field should be kept.  BitSize is the width of
// the field in bits, or 0 if this is a partword ATOMIC_LOADW_* instruction.
MachineBasicBlock *
SystemZTargetLowering::emitAtomicLoadMinMax(MachineInstr *MI,
                                            MachineBasicBlock *MBB,
                                            unsigned CompareOpcode,
                                            unsigned KeepOldMask,
                                            unsigned BitSize) const {
  MachineFunction &MF = *MBB->getParent();
  const SystemZInstrInfo *TII =
      static_cast<const SystemZInstrInfo *>(Subtarget.getInstrInfo());
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool IsSubWord = (BitSize < 32);

  // Extract the operands.  Base can be a register or a frame index.
  unsigned Dest        = MI->getOperand(0).getReg();
  MachineOperand Base  = earlyUseOperand(MI->getOperand(1));
  int64_t  Disp        = MI->getOperand(2).getImm();
  unsigned Src2        = MI->getOperand(3).getReg();
  unsigned BitShift    = (IsSubWord ? MI->getOperand(4).getReg() : 0);
  unsigned NegBitShift = (IsSubWord ? MI->getOperand(5).getReg() : 0);
  DebugLoc DL          = MI->getDebugLoc();
  if (IsSubWord)
    BitSize = MI->getOperand(6).getImm();

  // Subword operations use 32-bit registers.
  const TargetRegisterClass *RC = (BitSize <= 32 ?
                                   &SystemZ::GR32BitRegClass :
                                   &SystemZ::GR64BitRegClass);
  unsigned LOpcode  = BitSize <= 32 ? SystemZ::L  : SystemZ::LG;
  unsigned CSOpcode = BitSize <= 32 ? SystemZ::CS : SystemZ::CSG;

  // Get the right opcodes for the displacement.
  LOpcode  = TII->getOpcodeForOffset(LOpcode,  Disp);
  CSOpcode = TII->getOpcodeForOffset(CSOpcode, Disp);
  assert(LOpcode && CSOpcode && "Displacement out of range");

  // Create virtual registers for temporary results.
  unsigned OrigVal       = MRI.createVirtualRegister(RC);
  unsigned OldVal        = MRI.createVirtualRegister(RC);
  unsigned NewVal        = MRI.createVirtualRegister(RC);
  unsigned RotatedOldVal = (IsSubWord ? MRI.createVirtualRegister(RC) : OldVal);
  unsigned RotatedAltVal = (IsSubWord ? MRI.createVirtualRegister(RC) : Src2);
  unsigned RotatedNewVal = (IsSubWord ? MRI.createVirtualRegister(RC) : NewVal);

  // Insert 3 basic blocks for the loop.
  MachineBasicBlock *StartMBB  = MBB;
  MachineBasicBlock *DoneMBB   = splitBlockBefore(MI, MBB);
  MachineBasicBlock *LoopMBB   = emitBlockAfter(StartMBB);
  MachineBasicBlock *UseAltMBB = emitBlockAfter(LoopMBB);
  MachineBasicBlock *UpdateMBB = emitBlockAfter(UseAltMBB);

  //  StartMBB:
  //   ...
  //   %OrigVal     = L Disp(%Base)
  //   # fall through to LoopMMB
  MBB = StartMBB;
  BuildMI(MBB, DL, TII->get(LOpcode), OrigVal)
    .addOperand(Base).addImm(Disp).addReg(0);
  MBB->addSuccessor(LoopMBB);

  //  LoopMBB:
  //   %OldVal        = phi [ %OrigVal, StartMBB ], [ %Dest, UpdateMBB ]
  //   %RotatedOldVal = RLL %OldVal, 0(%BitShift)
  //   CompareOpcode %RotatedOldVal, %Src2
  //   BRC KeepOldMask, UpdateMBB
  MBB = LoopMBB;
  BuildMI(MBB, DL, TII->get(SystemZ::PHI), OldVal)
    .addReg(OrigVal).addMBB(StartMBB)
    .addReg(Dest).addMBB(UpdateMBB);
  if (IsSubWord)
    BuildMI(MBB, DL, TII->get(SystemZ::RLL), RotatedOldVal)
      .addReg(OldVal).addReg(BitShift).addImm(0);
  BuildMI(MBB, DL, TII->get(CompareOpcode))
    .addReg(RotatedOldVal).addReg(Src2);
  BuildMI(MBB, DL, TII->get(SystemZ::BRC))
    .addImm(SystemZ::CCMASK_ICMP).addImm(KeepOldMask).addMBB(UpdateMBB);
  MBB->addSuccessor(UpdateMBB);
  MBB->addSuccessor(UseAltMBB);

  //  UseAltMBB:
  //   %RotatedAltVal = RISBG %RotatedOldVal, %Src2, 32, 31 + BitSize, 0
  //   # fall through to UpdateMMB
  MBB = UseAltMBB;
  if (IsSubWord)
    BuildMI(MBB, DL, TII->get(SystemZ::RISBG32), RotatedAltVal)
      .addReg(RotatedOldVal).addReg(Src2)
      .addImm(32).addImm(31 + BitSize).addImm(0);
  MBB->addSuccessor(UpdateMBB);

  //  UpdateMBB:
  //   %RotatedNewVal = PHI [ %RotatedOldVal, LoopMBB ],
  //                        [ %RotatedAltVal, UseAltMBB ]
  //   %NewVal        = RLL %RotatedNewVal, 0(%NegBitShift)
  //   %Dest          = CS %OldVal, %NewVal, Disp(%Base)
  //   JNE LoopMBB
  //   # fall through to DoneMMB
  MBB = UpdateMBB;
  BuildMI(MBB, DL, TII->get(SystemZ::PHI), RotatedNewVal)
    .addReg(RotatedOldVal).addMBB(LoopMBB)
    .addReg(RotatedAltVal).addMBB(UseAltMBB);
  if (IsSubWord)
    BuildMI(MBB, DL, TII->get(SystemZ::RLL), NewVal)
      .addReg(RotatedNewVal).addReg(NegBitShift).addImm(0);
  BuildMI(MBB, DL, TII->get(CSOpcode), Dest)
    .addReg(OldVal).addReg(NewVal).addOperand(Base).addImm(Disp);
  BuildMI(MBB, DL, TII->get(SystemZ::BRC))
    .addImm(SystemZ::CCMASK_CS).addImm(SystemZ::CCMASK_CS_NE).addMBB(LoopMBB);
  MBB->addSuccessor(LoopMBB);
  MBB->addSuccessor(DoneMBB);

  MI->eraseFromParent();
  return DoneMBB;
}

// Implement EmitInstrWithCustomInserter for pseudo ATOMIC_CMP_SWAPW
// instruction MI.
MachineBasicBlock *
SystemZTargetLowering::emitAtomicCmpSwapW(MachineInstr *MI,
                                          MachineBasicBlock *MBB) const {
  MachineFunction &MF = *MBB->getParent();
  const SystemZInstrInfo *TII =
      static_cast<const SystemZInstrInfo *>(Subtarget.getInstrInfo());
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // Extract the operands.  Base can be a register or a frame index.
  unsigned Dest        = MI->getOperand(0).getReg();
  MachineOperand Base  = earlyUseOperand(MI->getOperand(1));
  int64_t  Disp        = MI->getOperand(2).getImm();
  unsigned OrigCmpVal  = MI->getOperand(3).getReg();
  unsigned OrigSwapVal = MI->getOperand(4).getReg();
  unsigned BitShift    = MI->getOperand(5).getReg();
  unsigned NegBitShift = MI->getOperand(6).getReg();
  int64_t  BitSize     = MI->getOperand(7).getImm();
  DebugLoc DL          = MI->getDebugLoc();

  const TargetRegisterClass *RC = &SystemZ::GR32BitRegClass;

  // Get the right opcodes for the displacement.
  unsigned LOpcode  = TII->getOpcodeForOffset(SystemZ::L,  Disp);
  unsigned CSOpcode = TII->getOpcodeForOffset(SystemZ::CS, Disp);
  assert(LOpcode && CSOpcode && "Displacement out of range");

  // Create virtual registers for temporary results.
  unsigned OrigOldVal   = MRI.createVirtualRegister(RC);
  unsigned OldVal       = MRI.createVirtualRegister(RC);
  unsigned CmpVal       = MRI.createVirtualRegister(RC);
  unsigned SwapVal      = MRI.createVirtualRegister(RC);
  unsigned StoreVal     = MRI.createVirtualRegister(RC);
  unsigned RetryOldVal  = MRI.createVirtualRegister(RC);
  unsigned RetryCmpVal  = MRI.createVirtualRegister(RC);
  unsigned RetrySwapVal = MRI.createVirtualRegister(RC);

  // Insert 2 basic blocks for the loop.
  MachineBasicBlock *StartMBB = MBB;
  MachineBasicBlock *DoneMBB  = splitBlockBefore(MI, MBB);
  MachineBasicBlock *LoopMBB  = emitBlockAfter(StartMBB);
  MachineBasicBlock *SetMBB   = emitBlockAfter(LoopMBB);

  //  StartMBB:
  //   ...
  //   %OrigOldVal     = L Disp(%Base)
  //   # fall through to LoopMMB
  MBB = StartMBB;
  BuildMI(MBB, DL, TII->get(LOpcode), OrigOldVal)
    .addOperand(Base).addImm(Disp).addReg(0);
  MBB->addSuccessor(LoopMBB);

  //  LoopMBB:
  //   %OldVal        = phi [ %OrigOldVal, EntryBB ], [ %RetryOldVal, SetMBB ]
  //   %CmpVal        = phi [ %OrigCmpVal, EntryBB ], [ %RetryCmpVal, SetMBB ]
  //   %SwapVal       = phi [ %OrigSwapVal, EntryBB ], [ %RetrySwapVal, SetMBB ]
  //   %Dest          = RLL %OldVal, BitSize(%BitShift)
  //                      ^^ The low BitSize bits contain the field
  //                         of interest.
  //   %RetryCmpVal   = RISBG32 %CmpVal, %Dest, 32, 63-BitSize, 0
  //                      ^^ Replace the upper 32-BitSize bits of the
  //                         comparison value with those that we loaded,
  //                         so that we can use a full word comparison.
  //   CR %Dest, %RetryCmpVal
  //   JNE DoneMBB
  //   # Fall through to SetMBB
  MBB = LoopMBB;
  BuildMI(MBB, DL, TII->get(SystemZ::PHI), OldVal)
    .addReg(OrigOldVal).addMBB(StartMBB)
    .addReg(RetryOldVal).addMBB(SetMBB);
  BuildMI(MBB, DL, TII->get(SystemZ::PHI), CmpVal)
    .addReg(OrigCmpVal).addMBB(StartMBB)
    .addReg(RetryCmpVal).addMBB(SetMBB);
  BuildMI(MBB, DL, TII->get(SystemZ::PHI), SwapVal)
    .addReg(OrigSwapVal).addMBB(StartMBB)
    .addReg(RetrySwapVal).addMBB(SetMBB);
  BuildMI(MBB, DL, TII->get(SystemZ::RLL), Dest)
    .addReg(OldVal).addReg(BitShift).addImm(BitSize);
  BuildMI(MBB, DL, TII->get(SystemZ::RISBG32), RetryCmpVal)
    .addReg(CmpVal).addReg(Dest).addImm(32).addImm(63 - BitSize).addImm(0);
  BuildMI(MBB, DL, TII->get(SystemZ::CR))
    .addReg(Dest).addReg(RetryCmpVal);
  BuildMI(MBB, DL, TII->get(SystemZ::BRC))
    .addImm(SystemZ::CCMASK_ICMP)
    .addImm(SystemZ::CCMASK_CMP_NE).addMBB(DoneMBB);
  MBB->addSuccessor(DoneMBB);
  MBB->addSuccessor(SetMBB);

  //  SetMBB:
  //   %RetrySwapVal = RISBG32 %SwapVal, %Dest, 32, 63-BitSize, 0
  //                      ^^ Replace the upper 32-BitSize bits of the new
  //                         value with those that we loaded.
  //   %StoreVal    = RLL %RetrySwapVal, -BitSize(%NegBitShift)
  //                      ^^ Rotate the new field to its proper position.
  //   %RetryOldVal = CS %Dest, %StoreVal, Disp(%Base)
  //   JNE LoopMBB
  //   # fall through to ExitMMB
  MBB = SetMBB;
  BuildMI(MBB, DL, TII->get(SystemZ::RISBG32), RetrySwapVal)
    .addReg(SwapVal).addReg(Dest).addImm(32).addImm(63 - BitSize).addImm(0);
  BuildMI(MBB, DL, TII->get(SystemZ::RLL), StoreVal)
    .addReg(RetrySwapVal).addReg(NegBitShift).addImm(-BitSize);
  BuildMI(MBB, DL, TII->get(CSOpcode), RetryOldVal)
    .addReg(OldVal).addReg(StoreVal).addOperand(Base).addImm(Disp);
  BuildMI(MBB, DL, TII->get(SystemZ::BRC))
    .addImm(SystemZ::CCMASK_CS).addImm(SystemZ::CCMASK_CS_NE).addMBB(LoopMBB);
  MBB->addSuccessor(LoopMBB);
  MBB->addSuccessor(DoneMBB);

  MI->eraseFromParent();
  return DoneMBB;
}

// Emit an extension from a GR32 or GR64 to a GR128.  ClearEven is true
// if the high register of the GR128 value must be cleared or false if
// it's "don't care".  SubReg is subreg_l32 when extending a GR32
// and subreg_l64 when extending a GR64.
MachineBasicBlock *
SystemZTargetLowering::emitExt128(MachineInstr *MI,
                                  MachineBasicBlock *MBB,
                                  bool ClearEven, unsigned SubReg) const {
  MachineFunction &MF = *MBB->getParent();
  const SystemZInstrInfo *TII =
      static_cast<const SystemZInstrInfo *>(Subtarget.getInstrInfo());
  MachineRegisterInfo &MRI = MF.getRegInfo();
  DebugLoc DL = MI->getDebugLoc();

  unsigned Dest  = MI->getOperand(0).getReg();
  unsigned Src   = MI->getOperand(1).getReg();
  unsigned In128 = MRI.createVirtualRegister(&SystemZ::GR128BitRegClass);

  BuildMI(*MBB, MI, DL, TII->get(TargetOpcode::IMPLICIT_DEF), In128);
  if (ClearEven) {
    unsigned NewIn128 = MRI.createVirtualRegister(&SystemZ::GR128BitRegClass);
    unsigned Zero64   = MRI.createVirtualRegister(&SystemZ::GR64BitRegClass);

    BuildMI(*MBB, MI, DL, TII->get(SystemZ::LLILL), Zero64)
      .addImm(0);
    BuildMI(*MBB, MI, DL, TII->get(TargetOpcode::INSERT_SUBREG), NewIn128)
      .addReg(In128).addReg(Zero64).addImm(SystemZ::subreg_h64);
    In128 = NewIn128;
  }
  BuildMI(*MBB, MI, DL, TII->get(TargetOpcode::INSERT_SUBREG), Dest)
    .addReg(In128).addReg(Src).addImm(SubReg);

  MI->eraseFromParent();
  return MBB;
}

MachineBasicBlock *
SystemZTargetLowering::emitMemMemWrapper(MachineInstr *MI,
                                         MachineBasicBlock *MBB,
                                         unsigned Opcode) const {
  MachineFunction &MF = *MBB->getParent();
  const SystemZInstrInfo *TII =
      static_cast<const SystemZInstrInfo *>(Subtarget.getInstrInfo());
  MachineRegisterInfo &MRI = MF.getRegInfo();
  DebugLoc DL = MI->getDebugLoc();

  MachineOperand DestBase = earlyUseOperand(MI->getOperand(0));
  uint64_t       DestDisp = MI->getOperand(1).getImm();
  MachineOperand SrcBase  = earlyUseOperand(MI->getOperand(2));
  uint64_t       SrcDisp  = MI->getOperand(3).getImm();
  uint64_t       Length   = MI->getOperand(4).getImm();

  // When generating more than one CLC, all but the last will need to
  // branch to the end when a difference is found.
  MachineBasicBlock *EndMBB = (Length > 256 && Opcode == SystemZ::CLC ?
                               splitBlockAfter(MI, MBB) : nullptr);

  // Check for the loop form, in which operand 5 is the trip count.
  if (MI->getNumExplicitOperands() > 5) {
    bool HaveSingleBase = DestBase.isIdenticalTo(SrcBase);

    uint64_t StartCountReg = MI->getOperand(5).getReg();
    uint64_t StartSrcReg   = forceReg(MI, SrcBase, TII);
    uint64_t StartDestReg  = (HaveSingleBase ? StartSrcReg :
                              forceReg(MI, DestBase, TII));

    const TargetRegisterClass *RC = &SystemZ::ADDR64BitRegClass;
    uint64_t ThisSrcReg  = MRI.createVirtualRegister(RC);
    uint64_t ThisDestReg = (HaveSingleBase ? ThisSrcReg :
                            MRI.createVirtualRegister(RC));
    uint64_t NextSrcReg  = MRI.createVirtualRegister(RC);
    uint64_t NextDestReg = (HaveSingleBase ? NextSrcReg :
                            MRI.createVirtualRegister(RC));

    RC = &SystemZ::GR64BitRegClass;
    uint64_t ThisCountReg = MRI.createVirtualRegister(RC);
    uint64_t NextCountReg = MRI.createVirtualRegister(RC);

    MachineBasicBlock *StartMBB = MBB;
    MachineBasicBlock *DoneMBB = splitBlockBefore(MI, MBB);
    MachineBasicBlock *LoopMBB = emitBlockAfter(StartMBB);
    MachineBasicBlock *NextMBB = (EndMBB ? emitBlockAfter(LoopMBB) : LoopMBB);

    //  StartMBB:
    //   # fall through to LoopMMB
    MBB->addSuccessor(LoopMBB);

    //  LoopMBB:
    //   %ThisDestReg = phi [ %StartDestReg, StartMBB ],
    //                      [ %NextDestReg, NextMBB ]
    //   %ThisSrcReg = phi [ %StartSrcReg, StartMBB ],
    //                     [ %NextSrcReg, NextMBB ]
    //   %ThisCountReg = phi [ %StartCountReg, StartMBB ],
    //                       [ %NextCountReg, NextMBB ]
    //   ( PFD 2, 768+DestDisp(%ThisDestReg) )
    //   Opcode DestDisp(256,%ThisDestReg), SrcDisp(%ThisSrcReg)
    //   ( JLH EndMBB )
    //
    // The prefetch is used only for MVC.  The JLH is used only for CLC.
    MBB = LoopMBB;

    BuildMI(MBB, DL, TII->get(SystemZ::PHI), ThisDestReg)
      .addReg(StartDestReg).addMBB(StartMBB)
      .addReg(NextDestReg).addMBB(NextMBB);
    if (!HaveSingleBase)
      BuildMI(MBB, DL, TII->get(SystemZ::PHI), ThisSrcReg)
        .addReg(StartSrcReg).addMBB(StartMBB)
        .addReg(NextSrcReg).addMBB(NextMBB);
    BuildMI(MBB, DL, TII->get(SystemZ::PHI), ThisCountReg)
      .addReg(StartCountReg).addMBB(StartMBB)
      .addReg(NextCountReg).addMBB(NextMBB);
    if (Opcode == SystemZ::MVC)
      BuildMI(MBB, DL, TII->get(SystemZ::PFD))
        .addImm(SystemZ::PFD_WRITE)
        .addReg(ThisDestReg).addImm(DestDisp + 768).addReg(0);
    BuildMI(MBB, DL, TII->get(Opcode))
      .addReg(ThisDestReg).addImm(DestDisp).addImm(256)
      .addReg(ThisSrcReg).addImm(SrcDisp);
    if (EndMBB) {
      BuildMI(MBB, DL, TII->get(SystemZ::BRC))
        .addImm(SystemZ::CCMASK_ICMP).addImm(SystemZ::CCMASK_CMP_NE)
        .addMBB(EndMBB);
      MBB->addSuccessor(EndMBB);
      MBB->addSuccessor(NextMBB);
    }

    // NextMBB:
    //   %NextDestReg = LA 256(%ThisDestReg)
    //   %NextSrcReg = LA 256(%ThisSrcReg)
    //   %NextCountReg = AGHI %ThisCountReg, -1
    //   CGHI %NextCountReg, 0
    //   JLH LoopMBB
    //   # fall through to DoneMMB
    //
    // The AGHI, CGHI and JLH should be converted to BRCTG by later passes.
    MBB = NextMBB;

    BuildMI(MBB, DL, TII->get(SystemZ::LA), NextDestReg)
      .addReg(ThisDestReg).addImm(256).addReg(0);
    if (!HaveSingleBase)
      BuildMI(MBB, DL, TII->get(SystemZ::LA), NextSrcReg)
        .addReg(ThisSrcReg).addImm(256).addReg(0);
    BuildMI(MBB, DL, TII->get(SystemZ::AGHI), NextCountReg)
      .addReg(ThisCountReg).addImm(-1);
    BuildMI(MBB, DL, TII->get(SystemZ::CGHI))
      .addReg(NextCountReg).addImm(0);
    BuildMI(MBB, DL, TII->get(SystemZ::BRC))
      .addImm(SystemZ::CCMASK_ICMP).addImm(SystemZ::CCMASK_CMP_NE)
      .addMBB(LoopMBB);
    MBB->addSuccessor(LoopMBB);
    MBB->addSuccessor(DoneMBB);

    DestBase = MachineOperand::CreateReg(NextDestReg, false);
    SrcBase = MachineOperand::CreateReg(NextSrcReg, false);
    Length &= 255;
    MBB = DoneMBB;
  }
  // Handle any remaining bytes with straight-line code.
  while (Length > 0) {
    uint64_t ThisLength = std::min(Length, uint64_t(256));
    // The previous iteration might have created out-of-range displacements.
    // Apply them using LAY if so.
    if (!isUInt<12>(DestDisp)) {
      unsigned Reg = MRI.createVirtualRegister(&SystemZ::ADDR64BitRegClass);
      BuildMI(*MBB, MI, MI->getDebugLoc(), TII->get(SystemZ::LAY), Reg)
        .addOperand(DestBase).addImm(DestDisp).addReg(0);
      DestBase = MachineOperand::CreateReg(Reg, false);
      DestDisp = 0;
    }
    if (!isUInt<12>(SrcDisp)) {
      unsigned Reg = MRI.createVirtualRegister(&SystemZ::ADDR64BitRegClass);
      BuildMI(*MBB, MI, MI->getDebugLoc(), TII->get(SystemZ::LAY), Reg)
        .addOperand(SrcBase).addImm(SrcDisp).addReg(0);
      SrcBase = MachineOperand::CreateReg(Reg, false);
      SrcDisp = 0;
    }
    BuildMI(*MBB, MI, DL, TII->get(Opcode))
      .addOperand(DestBase).addImm(DestDisp).addImm(ThisLength)
      .addOperand(SrcBase).addImm(SrcDisp);
    DestDisp += ThisLength;
    SrcDisp += ThisLength;
    Length -= ThisLength;
    // If there's another CLC to go, branch to the end if a difference
    // was found.
    if (EndMBB && Length > 0) {
      MachineBasicBlock *NextMBB = splitBlockBefore(MI, MBB);
      BuildMI(MBB, DL, TII->get(SystemZ::BRC))
        .addImm(SystemZ::CCMASK_ICMP).addImm(SystemZ::CCMASK_CMP_NE)
        .addMBB(EndMBB);
      MBB->addSuccessor(EndMBB);
      MBB->addSuccessor(NextMBB);
      MBB = NextMBB;
    }
  }
  if (EndMBB) {
    MBB->addSuccessor(EndMBB);
    MBB = EndMBB;
    MBB->addLiveIn(SystemZ::CC);
  }

  MI->eraseFromParent();
  return MBB;
}

// Decompose string pseudo-instruction MI into a loop that continually performs
// Opcode until CC != 3.
MachineBasicBlock *
SystemZTargetLowering::emitStringWrapper(MachineInstr *MI,
                                         MachineBasicBlock *MBB,
                                         unsigned Opcode) const {
  MachineFunction &MF = *MBB->getParent();
  const SystemZInstrInfo *TII =
      static_cast<const SystemZInstrInfo *>(Subtarget.getInstrInfo());
  MachineRegisterInfo &MRI = MF.getRegInfo();
  DebugLoc DL = MI->getDebugLoc();

  uint64_t End1Reg   = MI->getOperand(0).getReg();
  uint64_t Start1Reg = MI->getOperand(1).getReg();
  uint64_t Start2Reg = MI->getOperand(2).getReg();
  uint64_t CharReg   = MI->getOperand(3).getReg();

  const TargetRegisterClass *RC = &SystemZ::GR64BitRegClass;
  uint64_t This1Reg = MRI.createVirtualRegister(RC);
  uint64_t This2Reg = MRI.createVirtualRegister(RC);
  uint64_t End2Reg  = MRI.createVirtualRegister(RC);

  MachineBasicBlock *StartMBB = MBB;
  MachineBasicBlock *DoneMBB = splitBlockBefore(MI, MBB);
  MachineBasicBlock *LoopMBB = emitBlockAfter(StartMBB);

  //  StartMBB:
  //   # fall through to LoopMMB
  MBB->addSuccessor(LoopMBB);

  //  LoopMBB:
  //   %This1Reg = phi [ %Start1Reg, StartMBB ], [ %End1Reg, LoopMBB ]
  //   %This2Reg = phi [ %Start2Reg, StartMBB ], [ %End2Reg, LoopMBB ]
  //   R0L = %CharReg
  //   %End1Reg, %End2Reg = CLST %This1Reg, %This2Reg -- uses R0L
  //   JO LoopMBB
  //   # fall through to DoneMMB
  //
  // The load of R0L can be hoisted by post-RA LICM.
  MBB = LoopMBB;

  BuildMI(MBB, DL, TII->get(SystemZ::PHI), This1Reg)
    .addReg(Start1Reg).addMBB(StartMBB)
    .addReg(End1Reg).addMBB(LoopMBB);
  BuildMI(MBB, DL, TII->get(SystemZ::PHI), This2Reg)
    .addReg(Start2Reg).addMBB(StartMBB)
    .addReg(End2Reg).addMBB(LoopMBB);
  BuildMI(MBB, DL, TII->get(TargetOpcode::COPY), SystemZ::R0L).addReg(CharReg);
  BuildMI(MBB, DL, TII->get(Opcode))
    .addReg(End1Reg, RegState::Define).addReg(End2Reg, RegState::Define)
    .addReg(This1Reg).addReg(This2Reg);
  BuildMI(MBB, DL, TII->get(SystemZ::BRC))
    .addImm(SystemZ::CCMASK_ANY).addImm(SystemZ::CCMASK_3).addMBB(LoopMBB);
  MBB->addSuccessor(LoopMBB);
  MBB->addSuccessor(DoneMBB);

  DoneMBB->addLiveIn(SystemZ::CC);

  MI->eraseFromParent();
  return DoneMBB;
}

// Update TBEGIN instruction with final opcode and register clobbers.
MachineBasicBlock *
SystemZTargetLowering::emitTransactionBegin(MachineInstr *MI,
                                            MachineBasicBlock *MBB,
                                            unsigned Opcode,
                                            bool NoFloat) const {
  MachineFunction &MF = *MBB->getParent();
  const TargetFrameLowering *TFI = Subtarget.getFrameLowering();
  const SystemZInstrInfo *TII = Subtarget.getInstrInfo();

  // Update opcode.
  MI->setDesc(TII->get(Opcode));

  // We cannot handle a TBEGIN that clobbers the stack or frame pointer.
  // Make sure to add the corresponding GRSM bits if they are missing.
  uint64_t Control = MI->getOperand(2).getImm();
  static const unsigned GPRControlBit[16] = {
    0x8000, 0x8000, 0x4000, 0x4000, 0x2000, 0x2000, 0x1000, 0x1000,
    0x0800, 0x0800, 0x0400, 0x0400, 0x0200, 0x0200, 0x0100, 0x0100
  };
  Control |= GPRControlBit[15];
  if (TFI->hasFP(MF))
    Control |= GPRControlBit[11];
  MI->getOperand(2).setImm(Control);

  // Add GPR clobbers.
  for (int I = 0; I < 16; I++) {
    if ((Control & GPRControlBit[I]) == 0) {
      unsigned Reg = SystemZMC::GR64Regs[I];
      MI->addOperand(MachineOperand::CreateReg(Reg, true, true));
    }
  }

  // Add FPR/VR clobbers.
  if (!NoFloat && (Control & 4) != 0) {
    if (Subtarget.hasVector()) {
      for (int I = 0; I < 32; I++) {
        unsigned Reg = SystemZMC::VR128Regs[I];
        MI->addOperand(MachineOperand::CreateReg(Reg, true, true));
      }
    } else {
      for (int I = 0; I < 16; I++) {
        unsigned Reg = SystemZMC::FP64Regs[I];
        MI->addOperand(MachineOperand::CreateReg(Reg, true, true));
      }
    }
  }

  return MBB;
}

MachineBasicBlock *
SystemZTargetLowering::emitLoadAndTestCmp0(MachineInstr *MI,
                                          MachineBasicBlock *MBB,
                                          unsigned Opcode) const {
  MachineFunction &MF = *MBB->getParent();
  MachineRegisterInfo *MRI = &MF.getRegInfo();
  const SystemZInstrInfo *TII =
      static_cast<const SystemZInstrInfo *>(Subtarget.getInstrInfo());
  DebugLoc DL = MI->getDebugLoc();

  unsigned SrcReg = MI->getOperand(0).getReg();

  // Create new virtual register of the same class as source.
  const TargetRegisterClass *RC = MRI->getRegClass(SrcReg);
  unsigned DstReg = MRI->createVirtualRegister(RC);

  // Replace pseudo with a normal load-and-test that models the def as
  // well.
  BuildMI(*MBB, MI, DL, TII->get(Opcode), DstReg)
    .addReg(SrcReg);
  MI->eraseFromParent();

  return MBB;
}

MachineBasicBlock *SystemZTargetLowering::
EmitInstrWithCustomInserter(MachineInstr *MI, MachineBasicBlock *MBB) const {
  switch (MI->getOpcode()) {
  case SystemZ::Select32Mux:
  case SystemZ::Select32:
  case SystemZ::SelectF32:
  case SystemZ::Select64:
  case SystemZ::SelectF64:
  case SystemZ::SelectF128:
    return emitSelect(MI, MBB);

  case SystemZ::CondStore8Mux:
    return emitCondStore(MI, MBB, SystemZ::STCMux, 0, false);
  case SystemZ::CondStore8MuxInv:
    return emitCondStore(MI, MBB, SystemZ::STCMux, 0, true);
  case SystemZ::CondStore16Mux:
    return emitCondStore(MI, MBB, SystemZ::STHMux, 0, false);
  case SystemZ::CondStore16MuxInv:
    return emitCondStore(MI, MBB, SystemZ::STHMux, 0, true);
  case SystemZ::CondStore8:
    return emitCondStore(MI, MBB, SystemZ::STC, 0, false);
  case SystemZ::CondStore8Inv:
    return emitCondStore(MI, MBB, SystemZ::STC, 0, true);
  case SystemZ::CondStore16:
    return emitCondStore(MI, MBB, SystemZ::STH, 0, false);
  case SystemZ::CondStore16Inv:
    return emitCondStore(MI, MBB, SystemZ::STH, 0, true);
  case SystemZ::CondStore32:
    return emitCondStore(MI, MBB, SystemZ::ST, SystemZ::STOC, false);
  case SystemZ::CondStore32Inv:
    return emitCondStore(MI, MBB, SystemZ::ST, SystemZ::STOC, true);
  case SystemZ::CondStore64:
    return emitCondStore(MI, MBB, SystemZ::STG, SystemZ::STOCG, false);
  case SystemZ::CondStore64Inv:
    return emitCondStore(MI, MBB, SystemZ::STG, SystemZ::STOCG, true);
  case SystemZ::CondStoreF32:
    return emitCondStore(MI, MBB, SystemZ::STE, 0, false);
  case SystemZ::CondStoreF32Inv:
    return emitCondStore(MI, MBB, SystemZ::STE, 0, true);
  case SystemZ::CondStoreF64:
    return emitCondStore(MI, MBB, SystemZ::STD, 0, false);
  case SystemZ::CondStoreF64Inv:
    return emitCondStore(MI, MBB, SystemZ::STD, 0, true);

  case SystemZ::AEXT128_64:
    return emitExt128(MI, MBB, false, SystemZ::subreg_l64);
  case SystemZ::ZEXT128_32:
    return emitExt128(MI, MBB, true, SystemZ::subreg_l32);
  case SystemZ::ZEXT128_64:
    return emitExt128(MI, MBB, true, SystemZ::subreg_l64);

  case SystemZ::ATOMIC_SWAPW:
    return emitAtomicLoadBinary(MI, MBB, 0, 0);
  case SystemZ::ATOMIC_SWAP_32:
    return emitAtomicLoadBinary(MI, MBB, 0, 32);
  case SystemZ::ATOMIC_SWAP_64:
    return emitAtomicLoadBinary(MI, MBB, 0, 64);

  case SystemZ::ATOMIC_LOADW_AR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::AR, 0);
  case SystemZ::ATOMIC_LOADW_AFI:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::AFI, 0);
  case SystemZ::ATOMIC_LOAD_AR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::AR, 32);
  case SystemZ::ATOMIC_LOAD_AHI:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::AHI, 32);
  case SystemZ::ATOMIC_LOAD_AFI:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::AFI, 32);
  case SystemZ::ATOMIC_LOAD_AGR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::AGR, 64);
  case SystemZ::ATOMIC_LOAD_AGHI:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::AGHI, 64);
  case SystemZ::ATOMIC_LOAD_AGFI:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::AGFI, 64);

  case SystemZ::ATOMIC_LOADW_SR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::SR, 0);
  case SystemZ::ATOMIC_LOAD_SR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::SR, 32);
  case SystemZ::ATOMIC_LOAD_SGR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::SGR, 64);

  case SystemZ::ATOMIC_LOADW_NR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NR, 0);
  case SystemZ::ATOMIC_LOADW_NILH:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILH, 0);
  case SystemZ::ATOMIC_LOAD_NR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NR, 32);
  case SystemZ::ATOMIC_LOAD_NILL:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILL, 32);
  case SystemZ::ATOMIC_LOAD_NILH:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILH, 32);
  case SystemZ::ATOMIC_LOAD_NILF:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILF, 32);
  case SystemZ::ATOMIC_LOAD_NGR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NGR, 64);
  case SystemZ::ATOMIC_LOAD_NILL64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILL64, 64);
  case SystemZ::ATOMIC_LOAD_NILH64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILH64, 64);
  case SystemZ::ATOMIC_LOAD_NIHL64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NIHL64, 64);
  case SystemZ::ATOMIC_LOAD_NIHH64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NIHH64, 64);
  case SystemZ::ATOMIC_LOAD_NILF64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILF64, 64);
  case SystemZ::ATOMIC_LOAD_NIHF64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NIHF64, 64);

  case SystemZ::ATOMIC_LOADW_OR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OR, 0);
  case SystemZ::ATOMIC_LOADW_OILH:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OILH, 0);
  case SystemZ::ATOMIC_LOAD_OR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OR, 32);
  case SystemZ::ATOMIC_LOAD_OILL:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OILL, 32);
  case SystemZ::ATOMIC_LOAD_OILH:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OILH, 32);
  case SystemZ::ATOMIC_LOAD_OILF:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OILF, 32);
  case SystemZ::ATOMIC_LOAD_OGR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OGR, 64);
  case SystemZ::ATOMIC_LOAD_OILL64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OILL64, 64);
  case SystemZ::ATOMIC_LOAD_OILH64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OILH64, 64);
  case SystemZ::ATOMIC_LOAD_OIHL64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OIHL64, 64);
  case SystemZ::ATOMIC_LOAD_OIHH64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OIHH64, 64);
  case SystemZ::ATOMIC_LOAD_OILF64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OILF64, 64);
  case SystemZ::ATOMIC_LOAD_OIHF64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::OIHF64, 64);

  case SystemZ::ATOMIC_LOADW_XR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::XR, 0);
  case SystemZ::ATOMIC_LOADW_XILF:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::XILF, 0);
  case SystemZ::ATOMIC_LOAD_XR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::XR, 32);
  case SystemZ::ATOMIC_LOAD_XILF:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::XILF, 32);
  case SystemZ::ATOMIC_LOAD_XGR:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::XGR, 64);
  case SystemZ::ATOMIC_LOAD_XILF64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::XILF64, 64);
  case SystemZ::ATOMIC_LOAD_XIHF64:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::XIHF64, 64);

  case SystemZ::ATOMIC_LOADW_NRi:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NR, 0, true);
  case SystemZ::ATOMIC_LOADW_NILHi:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILH, 0, true);
  case SystemZ::ATOMIC_LOAD_NRi:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NR, 32, true);
  case SystemZ::ATOMIC_LOAD_NILLi:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILL, 32, true);
  case SystemZ::ATOMIC_LOAD_NILHi:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILH, 32, true);
  case SystemZ::ATOMIC_LOAD_NILFi:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILF, 32, true);
  case SystemZ::ATOMIC_LOAD_NGRi:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NGR, 64, true);
  case SystemZ::ATOMIC_LOAD_NILL64i:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILL64, 64, true);
  case SystemZ::ATOMIC_LOAD_NILH64i:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILH64, 64, true);
  case SystemZ::ATOMIC_LOAD_NIHL64i:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NIHL64, 64, true);
  case SystemZ::ATOMIC_LOAD_NIHH64i:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NIHH64, 64, true);
  case SystemZ::ATOMIC_LOAD_NILF64i:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NILF64, 64, true);
  case SystemZ::ATOMIC_LOAD_NIHF64i:
    return emitAtomicLoadBinary(MI, MBB, SystemZ::NIHF64, 64, true);

  case SystemZ::ATOMIC_LOADW_MIN:
    return emitAtomicLoadMinMax(MI, MBB, SystemZ::CR,
                                SystemZ::CCMASK_CMP_LE, 0);
  case SystemZ::ATOMIC_LOAD_MIN_32:
    return emitAtomicLoadMinMax(MI, MBB, SystemZ::CR,
                                SystemZ::CCMASK_CMP_LE, 32);
  case SystemZ::ATOMIC_LOAD_MIN_64:
    return emitAtomicLoadMinMax(MI, MBB, SystemZ::CGR,
                                SystemZ::CCMASK_CMP_LE, 64);

  case SystemZ::ATOMIC_LOADW_MAX:
    return emitAtomicLoadMinMax(MI, MBB, SystemZ::CR,
                                SystemZ::CCMASK_CMP_GE, 0);
  case SystemZ::ATOMIC_LOAD_MAX_32:
    return emitAtomicLoadMinMax(MI, MBB, SystemZ::CR,
                                SystemZ::CCMASK_CMP_GE, 32);
  case SystemZ::ATOMIC_LOAD_MAX_64:
    return emitAtomicLoadMinMax(MI, MBB, SystemZ::CGR,
                                SystemZ::CCMASK_CMP_GE, 64);

  case SystemZ::ATOMIC_LOADW_UMIN:
    return emitAtomicLoadMinMax(MI, MBB, SystemZ::CLR,
                                SystemZ::CCMASK_CMP_LE, 0);
  case SystemZ::ATOMIC_LOAD_UMIN_32:
    return emitAtomicLoadMinMax(MI, MBB, SystemZ::CLR,
                                SystemZ::CCMASK_CMP_LE, 32);
  case SystemZ::ATOMIC_LOAD_UMIN_64:
    return emitAtomicLoadMinMax(MI, MBB, SystemZ::CLGR,
                                SystemZ::CCMASK_CMP_LE, 64);

  case SystemZ::ATOMIC_LOADW_UMAX:
    return emitAtomicLoadMinMax(MI, MBB, SystemZ::CLR,
                                SystemZ::CCMASK_CMP_GE, 0);
  case SystemZ::ATOMIC_LOAD_UMAX_32:
    return emitAtomicLoadMinMax(MI, MBB, SystemZ::CLR,
                                SystemZ::CCMASK_CMP_GE, 32);
  case SystemZ::ATOMIC_LOAD_UMAX_64:
    return emitAtomicLoadMinMax(MI, MBB, SystemZ::CLGR,
                                SystemZ::CCMASK_CMP_GE, 64);

  case SystemZ::ATOMIC_CMP_SWAPW:
    return emitAtomicCmpSwapW(MI, MBB);
  case SystemZ::MVCSequence:
  case SystemZ::MVCLoop:
    return emitMemMemWrapper(MI, MBB, SystemZ::MVC);
  case SystemZ::NCSequence:
  case SystemZ::NCLoop:
    return emitMemMemWrapper(MI, MBB, SystemZ::NC);
  case SystemZ::OCSequence:
  case SystemZ::OCLoop:
    return emitMemMemWrapper(MI, MBB, SystemZ::OC);
  case SystemZ::XCSequence:
  case SystemZ::XCLoop:
    return emitMemMemWrapper(MI, MBB, SystemZ::XC);
  case SystemZ::CLCSequence:
  case SystemZ::CLCLoop:
    return emitMemMemWrapper(MI, MBB, SystemZ::CLC);
  case SystemZ::CLSTLoop:
    return emitStringWrapper(MI, MBB, SystemZ::CLST);
  case SystemZ::MVSTLoop:
    return emitStringWrapper(MI, MBB, SystemZ::MVST);
  case SystemZ::SRSTLoop:
    return emitStringWrapper(MI, MBB, SystemZ::SRST);
  case SystemZ::TBEGIN:
    return emitTransactionBegin(MI, MBB, SystemZ::TBEGIN, false);
  case SystemZ::TBEGIN_nofloat:
    return emitTransactionBegin(MI, MBB, SystemZ::TBEGIN, true);
  case SystemZ::TBEGINC:
    return emitTransactionBegin(MI, MBB, SystemZ::TBEGINC, true);
  case SystemZ::LTEBRCompare_VecPseudo:
    return emitLoadAndTestCmp0(MI, MBB, SystemZ::LTEBR);
  case SystemZ::LTDBRCompare_VecPseudo:
    return emitLoadAndTestCmp0(MI, MBB, SystemZ::LTDBR);
  case SystemZ::LTXBRCompare_VecPseudo:
    return emitLoadAndTestCmp0(MI, MBB, SystemZ::LTXBR);

  default:
    llvm_unreachable("Unexpected instr type to insert");
  }
}
