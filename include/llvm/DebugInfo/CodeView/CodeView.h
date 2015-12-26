//===- CodeView.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CODEVIEW_H
#define LLVM_DEBUGINFO_CODEVIEW_CODEVIEW_H

#include <cinttypes>

namespace llvm {
namespace codeview {

enum class CallingConvention : uint8_t {
  NearC = 0x00,       // near right to left push, caller pops stack
  FarC = 0x01,        // far right to left push, caller pops stack
  NearPascal = 0x02,  // near left to right push, callee pops stack
  FarPascal = 0x03,   // far left to right push, callee pops stack
  NearFast = 0x04,    // near left to right push with regs, callee pops stack
  FarFast = 0x05,     // far left to right push with regs, callee pops stack
  NearStdCall = 0x07, // near standard call
  FarStdCall = 0x08,  // far standard call
  NearSysCall = 0x09, // near sys call
  FarSysCall = 0x0a,  // far sys call
  ThisCall = 0x0b,    // this call (this passed in register)
  MipsCall = 0x0c,    // Mips call
  Generic = 0x0d,     // Generic call sequence
  AlphaCall = 0x0e,   // Alpha call
  PpcCall = 0x0f,     // PPC call
  SHCall = 0x10,      // Hitachi SuperH call
  ArmCall = 0x11,     // ARM call
  AM33Call = 0x12,    // AM33 call
  TriCall = 0x13,     // TriCore Call
  SH5Call = 0x14,     // Hitachi SuperH-5 call
  M32RCall = 0x15,    // M32R Call
  ClrCall = 0x16,     // clr call
  Inline =
      0x17, // Marker for routines always inlined and thus lacking a convention
  NearVector = 0x18 // near left to right push with regs, callee pops stack
};

enum class ClassOptions : uint16_t {
  None = 0x0000,
  Packed = 0x0001,
  HasConstructorOrDestructor = 0x0002,
  HasOverloadedOperator = 0x0004,
  Nested = 0x0008,
  ContainsNestedClass = 0x0010,
  HasOverloadedAssignmentOperator = 0x0020,
  HasConversionOperator = 0x0040,
  ForwardReference = 0x0080,
  Scoped = 0x0100,
  HasUniqueName = 0x0200,
  Sealed = 0x0400,
  Intrinsic = 0x2000
};

inline ClassOptions operator|(ClassOptions a, ClassOptions b) {
  return static_cast<ClassOptions>(static_cast<uint16_t>(a) |
                                   static_cast<uint16_t>(b));
}

inline ClassOptions operator&(ClassOptions a, ClassOptions b) {
  return static_cast<ClassOptions>(static_cast<uint16_t>(a) &
                                   static_cast<uint16_t>(b));
}

inline ClassOptions operator~(ClassOptions a) {
  return static_cast<ClassOptions>(~static_cast<uint16_t>(a));
}

enum class FrameProcedureOptions : uint32_t {
  None = 0x00000000,
  HasAlloca = 0x00000001,
  HasSetJmp = 0x00000002,
  HasLongJmp = 0x00000004,
  HasInlineAssembly = 0x00000008,
  HasExceptionHandling = 0x00000010,
  MarkedInline = 0x00000020,
  HasStructuredExceptionHandling = 0x00000040,
  Naked = 0x00000080,
  SecurityChecks = 0x00000100,
  AsynchronousExceptionHandling = 0x00000200,
  NoStackOrderingForSecurityChecks = 0x00000400,
  Inlined = 0x00000800,
  StrictSecurityChecks = 0x00001000,
  SafeBuffers = 0x00002000,
  ProfileGuidedOptimization = 0x00040000,
  ValidProfileCounts = 0x00080000,
  OptimizedForSpeed = 0x00100000,
  GuardCfg = 0x00200000,
  GuardCfw = 0x00400000
};

inline FrameProcedureOptions operator|(FrameProcedureOptions a,
                                       FrameProcedureOptions b) {
  return static_cast<FrameProcedureOptions>(static_cast<uint32_t>(a) |
                                            static_cast<uint32_t>(b));
}

inline FrameProcedureOptions operator&(FrameProcedureOptions a,
                                       FrameProcedureOptions b) {
  return static_cast<FrameProcedureOptions>(static_cast<uint32_t>(a) &
                                            static_cast<uint32_t>(b));
}

inline FrameProcedureOptions operator~(FrameProcedureOptions a) {
  return static_cast<FrameProcedureOptions>(~static_cast<uint32_t>(a));
}

enum class FunctionOptions : uint8_t {
  None = 0x00,
  CxxReturnUdt = 0x01,
  Constructor = 0x02,
  ConstructorWithVirtualBases = 0x04
};

inline FunctionOptions operator|(FunctionOptions a, FunctionOptions b) {
  return static_cast<FunctionOptions>(static_cast<uint8_t>(a) |
                                      static_cast<uint8_t>(b));
}

inline FunctionOptions operator&(FunctionOptions a, FunctionOptions b) {
  return static_cast<FunctionOptions>(static_cast<uint8_t>(a) &
                                      static_cast<uint8_t>(b));
}

inline FunctionOptions operator~(FunctionOptions a) {
  return static_cast<FunctionOptions>(~static_cast<uint8_t>(a));
}

enum class HfaKind : uint8_t {
  None = 0x00,
  Float = 0x01,
  Double = 0x02,
  Other = 0x03
};

enum class MemberAccess : uint8_t {
  None = 0,
  Private = 1,
  Protected = 2,
  Public = 3
};

enum class MethodKind : uint8_t {
  Vanilla = 0x00,
  Virtual = 0x01,
  Static = 0x02,
  Friend = 0x03,
  IntroducingVirtual = 0x04,
  PureVirtual = 0x05,
  PureIntroducingVirtual = 0x06
};

enum class MethodOptions : uint16_t {
  None = 0x0000,
  Pseudo = 0x0020,
  CompilerGenerated = 0x0100,
  Sealed = 0x0200
};

inline MethodOptions operator|(MethodOptions a, MethodOptions b) {
  return static_cast<MethodOptions>(static_cast<uint16_t>(a) |
                                    static_cast<uint16_t>(b));
}

inline MethodOptions operator&(MethodOptions a, MethodOptions b) {
  return static_cast<MethodOptions>(static_cast<uint16_t>(a) &
                                    static_cast<uint16_t>(b));
}

inline MethodOptions operator~(MethodOptions a) {
  return static_cast<MethodOptions>(~static_cast<uint16_t>(a));
}

enum class ModifierOptions : uint16_t {
  None = 0x0000,
  Const = 0x0001,
  Volatile = 0x0002,
  Unaligned = 0x0004
};

inline ModifierOptions operator|(ModifierOptions a, ModifierOptions b) {
  return static_cast<ModifierOptions>(static_cast<uint16_t>(a) |
                                      static_cast<uint16_t>(b));
}

inline ModifierOptions operator&(ModifierOptions a, ModifierOptions b) {
  return static_cast<ModifierOptions>(static_cast<uint16_t>(a) &
                                      static_cast<uint16_t>(b));
}

inline ModifierOptions operator~(ModifierOptions a) {
  return static_cast<ModifierOptions>(~static_cast<uint16_t>(a));
}

enum class ModuleSubstreamKind : uint32_t {
  Symbols = 0xf1,
  Lines = 0xf2,
  StringTable = 0xf3,
  FileChecksums = 0xf4,
  FrameData = 0xf5,
  InlineeLines = 0xf6,
  CrossScopeImports = 0xf7,
  CrossScopeExports = 0xf8
};

enum class PointerKind : uint8_t {
  Near16 = 0x00,                // 16 bit pointer
  Far16 = 0x01,                 // 16:16 far pointer
  Huge16 = 0x02,                // 16:16 huge pointer
  BasedOnSegment = 0x03,        // based on segment
  BasedOnValue = 0x04,          // based on value of base
  BasedOnSegmentValue = 0x05,   // based on segment value of base
  BasedOnAddress = 0x06,        // based on address of base
  BasedOnSegmentAddress = 0x07, // based on segment address of base
  BasedOnType = 0x08,           // based on type
  BasedOnSelf = 0x09,           // based on self
  Near32 = 0x0a,                // 32 bit pointer
  Far32 = 0x0b,                 // 16:32 pointer
  Near64 = 0x0c                 // 64 bit pointer
};

enum class PointerMode : uint8_t {
  Pointer = 0x00,                 // "normal" pointer
  LValueReference = 0x01,         // "old" reference
  PointerToDataMember = 0x02,     // pointer to data member
  PointerToMemberFunction = 0x03, // pointer to member function
  RValueReference = 0x04          // r-value reference
};

enum class PointerOptions : uint32_t {
  None = 0x00000000,
  Flat32 = 0x00000100,
  Volatile = 0x00000200,
  Const = 0x00000400,
  Unaligned = 0x00000800,
  Restrict = 0x00001000,
  WinRTSmartPointer = 0x00080000
};

inline PointerOptions operator|(PointerOptions a, PointerOptions b) {
  return static_cast<PointerOptions>(static_cast<uint16_t>(a) |
                                     static_cast<uint16_t>(b));
}

inline PointerOptions operator&(PointerOptions a, PointerOptions b) {
  return static_cast<PointerOptions>(static_cast<uint16_t>(a) &
                                     static_cast<uint16_t>(b));
}

inline PointerOptions operator~(PointerOptions a) {
  return static_cast<PointerOptions>(~static_cast<uint16_t>(a));
}

enum class PointerToMemberRepresentation : uint16_t {
  Unknown = 0x00,                     // not specified (pre VC8)
  SingleInheritanceData = 0x01,       // member data, single inheritance
  MultipleInheritanceData = 0x02,     // member data, multiple inheritance
  VirtualInheritanceData = 0x03,      // member data, virtual inheritance
  GeneralData = 0x04,                 // member data, most general
  SingleInheritanceFunction = 0x05,   // member function, single inheritance
  MultipleInheritanceFunction = 0x06, // member function, multiple inheritance
  VirtualInheritanceFunction = 0x07,  // member function, virtual inheritance
  GeneralFunction = 0x08              // member function, most general
};

enum class TypeRecordKind : uint16_t {
  None = 0,

  VirtualTableShape = 0x000a,
  Label = 0x000e,
  EndPrecompiledHeader = 0x0014,

  Modifier = 0x1001,
  Pointer = 0x1002,
  Procedure = 0x1008,
  MemberFunction = 0x1009,

  Oem = 0x100f,
  Oem2 = 0x1011,

  ArgumentList = 0x1201,
  FieldList = 0x1203,
  BitField = 0x1205,
  MethodList = 0x1206,

  BaseClass = 0x1400,
  VirtualBaseClass = 0x1401,
  IndirectVirtualBaseClass = 0x1402,
  Index = 0x1404,
  VirtualFunctionTablePointer = 0x1409,

  Enumerate = 0x1502,
  Array = 0x1503,
  Class = 0x1504,
  Structure = 0x1505,
  Union = 0x1506,
  Enum = 0x1507,
  Alias = 0x150a,
  Member = 0x150d,
  StaticMember = 0x150e,
  Method = 0x150f,
  NestedType = 0x1510,
  OneMethod = 0x1511,
  VirtualFunctionTable = 0x151d,

  FunctionId = 0x1601,
  MemberFunctionId = 0x1602,
  BuildInfo = 0x1603,
  SubstringList = 0x1604,
  StringId = 0x1605,
  UdtSourceLine = 0x1606,

  SByte = 0x8000,
  Int16 = 0x8001,
  UInt16 = 0x8002,
  Int32 = 0x8003,
  UInt32 = 0x8004,
  Single = 0x8005,
  Double = 0x8006,
  Float80 = 0x8007,
  Float128 = 0x8008,
  Int64 = 0x8009,
  UInt64 = 0x800a,
  Float48 = 0x800b,
  Complex32 = 0x800c,
  Complex64 = 0x800d,
  Complex80 = 0x800e,
  Complex128 = 0x800f,
  VarString = 0x8010,

  Int128 = 0x8017,
  UInt128 = 0x8018,

  Decimal = 0x8019,
  Date = 0x801a,
  Utf8String = 0x801b,

  Float16 = 0x801c
};

enum class VirtualTableSlotKind : uint8_t {
  Near16 = 0x00,
  Far16 = 0x01,
  This = 0x02,
  Outer = 0x03,
  Meta = 0x04,
  Near = 0x05,
  Far = 0x06
};

enum class WindowsRTClassKind : uint8_t {
  None = 0x00,
  RefClass = 0x01,
  ValueClass = 0x02,
  Interface = 0x03
};
}
}

#endif
