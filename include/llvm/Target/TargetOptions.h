//===-- llvm/Target/TargetOptions.h - Target Options ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines command line option flags that are shared across various
// targets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_TARGETOPTIONS_H
#define LLVM_TARGET_TARGETOPTIONS_H

#include "llvm/MC/MCTargetOptions.h"
#include <string>

namespace llvm {
  class MachineFunction;
  class StringRef;

  // Possible float ABI settings. Used with FloatABIType in TargetOptions.h.
  namespace FloatABI {
    enum ABIType {
      Default, // Target-specific (either soft or hard depending on triple,etc).
      Soft, // Soft float.
      Hard  // Hard float.
    };
  }

  namespace FPOpFusion {
    enum FPOpFusionMode {
      Fast,     // Enable fusion of FP ops wherever it's profitable.
      Standard, // Only allow fusion of 'blessed' ops (currently just fmuladd).
      Strict    // Never fuse FP-ops.
    };
  }

  namespace JumpTable {
    enum JumpTableType {
      Single,          // Use a single table for all indirect jumptable calls.
      Arity,           // Use one table per number of function parameters.
      Simplified,      // Use one table per function type, with types projected
                       // into 4 types: pointer to non-function, struct,
                       // primitive, and function pointer.
      Full             // Use one table per unique function type
    };
  }

  namespace ThreadModel {
    enum Model {
      POSIX,  // POSIX Threads
      Single  // Single Threaded Environment
    };
  }

  enum class CFIntegrity {
    Sub,             // Use subtraction-based checks.
    Ror,             // Use rotation-based checks.
    Add              // Use addition-based checks. This depends on having
                     // sufficient alignment in the code and is usually not
                     // feasible.
  };

  class TargetOptions {
  public:
    TargetOptions()
        : PrintMachineCode(false), NoFramePointerElim(false),
          LessPreciseFPMADOption(false), UnsafeFPMath(false),
          NoInfsFPMath(false), NoNaNsFPMath(false),
          HonorSignDependentRoundingFPMathOption(false), UseSoftFloat(false),
          NoZerosInBSS(false), JITEmitDebugInfo(false),
          JITEmitDebugInfoToDisk(false), GuaranteedTailCallOpt(false),
          DisableTailCalls(false), StackAlignmentOverride(0),
          EnableFastISel(false), PositionIndependentExecutable(false),
          UseInitArray(false), DisableIntegratedAS(false),
          CompressDebugSections(false), FunctionSections(false),
          DataSections(false), TrapUnreachable(false), TrapFuncName(),
          FloatABIType(FloatABI::Default),
          AllowFPOpFusion(FPOpFusion::Standard), JTType(JumpTable::Single),
          FCFI(false), ThreadModel(ThreadModel::POSIX),
          CFIType(CFIntegrity::Sub), CFIEnforcing(false), CFIFuncName() {}

    /// PrintMachineCode - This flag is enabled when the -print-machineinstrs
    /// option is specified on the command line, and should enable debugging
    /// output from the code generator.
    unsigned PrintMachineCode : 1;

    /// NoFramePointerElim - This flag is enabled when the -disable-fp-elim is
    /// specified on the command line.  If the target supports the frame pointer
    /// elimination optimization, this option should disable it.
    unsigned NoFramePointerElim : 1;

    /// DisableFramePointerElim - This returns true if frame pointer elimination
    /// optimization should be disabled for the given machine function.
    bool DisableFramePointerElim(const MachineFunction &MF) const;

    /// LessPreciseFPMAD - This flag is enabled when the
    /// -enable-fp-mad is specified on the command line.  When this flag is off
    /// (the default), the code generator is not allowed to generate mad
    /// (multiply add) if the result is "less precise" than doing those
    /// operations individually.
    unsigned LessPreciseFPMADOption : 1;
    bool LessPreciseFPMAD() const;

    /// UnsafeFPMath - This flag is enabled when the
    /// -enable-unsafe-fp-math flag is specified on the command line.  When
    /// this flag is off (the default), the code generator is not allowed to
    /// produce results that are "less precise" than IEEE allows.  This includes
    /// use of X86 instructions like FSIN and FCOS instead of libcalls.
    /// UnsafeFPMath implies LessPreciseFPMAD.
    unsigned UnsafeFPMath : 1;

    /// NoInfsFPMath - This flag is enabled when the
    /// -enable-no-infs-fp-math flag is specified on the command line. When
    /// this flag is off (the default), the code generator is not allowed to
    /// assume the FP arithmetic arguments and results are never +-Infs.
    unsigned NoInfsFPMath : 1;

    /// NoNaNsFPMath - This flag is enabled when the
    /// -enable-no-nans-fp-math flag is specified on the command line. When
    /// this flag is off (the default), the code generator is not allowed to
    /// assume the FP arithmetic arguments and results are never NaNs.
    unsigned NoNaNsFPMath : 1;

    /// HonorSignDependentRoundingFPMath - This returns true when the
    /// -enable-sign-dependent-rounding-fp-math is specified.  If this returns
    /// false (the default), the code generator is allowed to assume that the
    /// rounding behavior is the default (round-to-zero for all floating point
    /// to integer conversions, and round-to-nearest for all other arithmetic
    /// truncations).  If this is enabled (set to true), the code generator must
    /// assume that the rounding mode may dynamically change.
    unsigned HonorSignDependentRoundingFPMathOption : 1;
    bool HonorSignDependentRoundingFPMath() const;

    /// UseSoftFloat - This flag is enabled when the -soft-float flag is
    /// specified on the command line.  When this flag is on, the code generator
    /// will generate libcalls to the software floating point library instead of
    /// target FP instructions.
    unsigned UseSoftFloat : 1;

    /// NoZerosInBSS - By default some codegens place zero-initialized data to
    /// .bss section. This flag disables such behaviour (necessary, e.g. for
    /// crt*.o compiling).
    unsigned NoZerosInBSS : 1;

    /// JITEmitDebugInfo - This flag indicates that the JIT should try to emit
    /// debug information and notify a debugger about it.
    unsigned JITEmitDebugInfo : 1;

    /// JITEmitDebugInfoToDisk - This flag indicates that the JIT should write
    /// the object files generated by the JITEmitDebugInfo flag to disk.  This
    /// flag is hidden and is only for debugging the debug info.
    unsigned JITEmitDebugInfoToDisk : 1;

    /// GuaranteedTailCallOpt - This flag is enabled when -tailcallopt is
    /// specified on the commandline. When the flag is on, participating targets
    /// will perform tail call optimization on all calls which use the fastcc
    /// calling convention and which satisfy certain target-independent
    /// criteria (being at the end of a function, having the same return type
    /// as their parent function, etc.), using an alternate ABI if necessary.
    unsigned GuaranteedTailCallOpt : 1;

    /// DisableTailCalls - This flag controls whether we will use tail calls.
    /// Disabling them may be useful to maintain a correct call stack.
    unsigned DisableTailCalls : 1;

    /// StackAlignmentOverride - Override default stack alignment for target.
    unsigned StackAlignmentOverride;

    /// EnableFastISel - This flag enables fast-path instruction selection
    /// which trades away generated code quality in favor of reducing
    /// compile time.
    unsigned EnableFastISel : 1;

    /// PositionIndependentExecutable - This flag indicates whether the code
    /// will eventually be linked into a single executable, despite the PIC
    /// relocation model being in use. It's value is undefined (and irrelevant)
    /// if the relocation model is anything other than PIC.
    unsigned PositionIndependentExecutable : 1;

    /// UseInitArray - Use .init_array instead of .ctors for static
    /// constructors.
    unsigned UseInitArray : 1;

    /// Disable the integrated assembler.
    unsigned DisableIntegratedAS : 1;

    /// Compress DWARF debug sections.
    unsigned CompressDebugSections : 1;

    /// Emit functions into separate sections.
    unsigned FunctionSections : 1;

    /// Emit data into separate sections.
    unsigned DataSections : 1;

    /// Emit target-specific trap instruction for 'unreachable' IR instructions.
    unsigned TrapUnreachable : 1;

    /// getTrapFunctionName - If this returns a non-empty string, this means
    /// isel should lower Intrinsic::trap to a call to the specified function
    /// name instead of an ISD::TRAP node.
    std::string TrapFuncName;
    StringRef getTrapFunctionName() const;

    /// FloatABIType - This setting is set by -float-abi=xxx option is specfied
    /// on the command line. This setting may either be Default, Soft, or Hard.
    /// Default selects the target's default behavior. Soft selects the ABI for
    /// UseSoftFloat, but does not indicate that FP hardware may not be used.
    /// Such a combination is unfortunately popular (e.g. arm-apple-darwin).
    /// Hard presumes that the normal FP ABI is used.
    FloatABI::ABIType FloatABIType;

    /// AllowFPOpFusion - This flag is set by the -fuse-fp-ops=xxx option.
    /// This controls the creation of fused FP ops that store intermediate
    /// results in higher precision than IEEE allows (E.g. FMAs).
    ///
    /// Fast mode - allows formation of fused FP ops whenever they're
    /// profitable.
    /// Standard mode - allow fusion only for 'blessed' FP ops. At present the
    /// only blessed op is the fmuladd intrinsic. In the future more blessed ops
    /// may be added.
    /// Strict mode - allow fusion only if/when it can be proven that the excess
    /// precision won't effect the result.
    ///
    /// Note: This option only controls formation of fused ops by the
    /// optimizers.  Fused operations that are explicitly specified (e.g. FMA
    /// via the llvm.fma.* intrinsic) will always be honored, regardless of
    /// the value of this option.
    FPOpFusion::FPOpFusionMode AllowFPOpFusion;

    /// JTType - This flag specifies the type of jump-instruction table to
    /// create for functions that have the jumptable attribute.
    JumpTable::JumpTableType JTType;

    /// FCFI - This flags controls whether or not forward-edge control-flow
    /// integrity is applied.
    bool FCFI;

    /// ThreadModel - This flag specifies the type of threading model to assume
    /// for things like atomics
    ThreadModel::Model ThreadModel;

    /// CFIType - This flag specifies the type of control-flow integrity check
    /// to add as a preamble to indirect calls.
    CFIntegrity CFIType;

    /// CFIEnforcing - This flags controls whether or not CFI violations cause
    /// the program to halt.
    bool CFIEnforcing;

    /// getCFIFuncName - If this returns a non-empty string, then this is the
    /// name of the function that will be called for each CFI violation in
    /// non-enforcing mode.
    std::string CFIFuncName;
    StringRef getCFIFuncName() const;

    /// Machine level options.
    MCTargetOptions MCOptions;
  };

// Comparison operators:


inline bool operator==(const TargetOptions &LHS,
                       const TargetOptions &RHS) {
#define ARE_EQUAL(X) LHS.X == RHS.X
  return
    ARE_EQUAL(UnsafeFPMath) &&
    ARE_EQUAL(NoInfsFPMath) &&
    ARE_EQUAL(NoNaNsFPMath) &&
    ARE_EQUAL(HonorSignDependentRoundingFPMathOption) &&
    ARE_EQUAL(UseSoftFloat) &&
    ARE_EQUAL(NoZerosInBSS) &&
    ARE_EQUAL(JITEmitDebugInfo) &&
    ARE_EQUAL(JITEmitDebugInfoToDisk) &&
    ARE_EQUAL(GuaranteedTailCallOpt) &&
    ARE_EQUAL(DisableTailCalls) &&
    ARE_EQUAL(StackAlignmentOverride) &&
    ARE_EQUAL(EnableFastISel) &&
    ARE_EQUAL(PositionIndependentExecutable) &&
    ARE_EQUAL(UseInitArray) &&
    ARE_EQUAL(TrapUnreachable) &&
    ARE_EQUAL(TrapFuncName) &&
    ARE_EQUAL(FloatABIType) &&
    ARE_EQUAL(AllowFPOpFusion) &&
    ARE_EQUAL(JTType) &&
    ARE_EQUAL(FCFI) &&
    ARE_EQUAL(ThreadModel) &&
    ARE_EQUAL(CFIType) &&
    ARE_EQUAL(CFIEnforcing) &&
    ARE_EQUAL(CFIFuncName) &&
    ARE_EQUAL(MCOptions);
#undef ARE_EQUAL
}

inline bool operator!=(const TargetOptions &LHS,
                       const TargetOptions &RHS) {
  return !(LHS == RHS);
}

} // End llvm namespace

#endif
