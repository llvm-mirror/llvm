//===- ValueTypes.td - ValueType definitions ---------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Value types - These values correspond to the register types defined in the
// MachineValueTypes.h file.  If you update anything here, you must update it
// there as well!
//
//===----------------------------------------------------------------------===//

class ValueType<int size, int value> {
  string Namespace = "MVT";
  int Size = size;
  int Value = value;
}

def OtherVT: ValueType<0  ,  1>;   // "Other" value
def i1     : ValueType<1  ,  2>;   // One bit boolean value
def i8     : ValueType<8  ,  3>;   // 8-bit integer value
def i16    : ValueType<16 ,  4>;   // 16-bit integer value
def i32    : ValueType<32 ,  5>;   // 32-bit integer value
def i64    : ValueType<64 ,  6>;   // 64-bit integer value
def i128   : ValueType<128,  7>;   // 128-bit integer value
def i160   : ValueType<160,  8>;   // 160-bit integer value
def i256   : ValueType<256,  9>;   // 256-bit integer value
def f16    : ValueType<16 , 10>;   // 16-bit floating point value
def f32    : ValueType<32 , 11>;   // 32-bit floating point value
def f64    : ValueType<64 , 12>;   // 64-bit floating point value
def f80    : ValueType<80 , 13>;   // 80-bit floating point value
def f128   : ValueType<128, 14>;   // 128-bit floating point value
def ppcf128: ValueType<128, 15>;   // PPC 128-bit floating point value

def v1i1   : ValueType<1 ,  16>;   //   1 x i1 vector value
def v2i1   : ValueType<2 ,  17>;   //   2 x i1 vector value
def v4i1   : ValueType<4 ,  18>;   //   4 x i1 vector value
def v8i1   : ValueType<8 ,  19>;   //   8 x i1 vector value
def v16i1  : ValueType<16,  20>;   //  16 x i1 vector value
def v32i1  : ValueType<32 , 21>;   //  32 x i1 vector value
def v64i1  : ValueType<64 , 22>;   //  64 x i1 vector value
def v128i1 : ValueType<128, 23>;   // 128 x i1 vector value
def v256i1 : ValueType<256, 24>;   // 256 x i1 vector value
def v512i1 : ValueType<512, 25>;   // 512 x i1 vector value
def v1024i1: ValueType<1024,26>;   //1024 x i1 vector value

def v1i8   : ValueType<8,   27>;   //  1 x i8  vector value
def v2i8   : ValueType<16 , 28>;   //  2 x i8  vector value
def v4i8   : ValueType<32 , 29>;   //  4 x i8  vector value
def v8i8   : ValueType<64 , 30>;   //  8 x i8  vector value
def v16i8  : ValueType<128, 31>;   // 16 x i8  vector value
def v32i8  : ValueType<256, 32>;   // 32 x i8  vector value
def v64i8  : ValueType<512, 33>;   // 64 x i8  vector value
def v128i8 : ValueType<1024,34>;   //128 x i8  vector value
def v256i8 : ValueType<2048,35>;   //256 x i8  vector value

def v1i16  : ValueType<16 , 36>;   //  1 x i16 vector value
def v2i16  : ValueType<32 , 37>;   //  2 x i16 vector value
def v3i16  : ValueType<48 , 38>;   //  3 x i16 vector value
def v4i16  : ValueType<64 , 39>;   //  4 x i16 vector value
def v8i16  : ValueType<128, 40>;   //  8 x i16 vector value
def v16i16 : ValueType<256, 41>;   // 16 x i16 vector value
def v32i16 : ValueType<512, 42>;   // 32 x i16 vector value
def v64i16 : ValueType<1024,43>;   // 64 x i16 vector value
def v128i16: ValueType<2048,44>;   //128 x i16 vector value

def v1i32    : ValueType<32 , 45>;   //  1 x i32 vector value
def v2i32    : ValueType<64 , 46>;   //  2 x i32 vector value
def v3i32    : ValueType<96 , 47>;   //  3 x i32 vector value
def v4i32    : ValueType<128, 48>;   //  4 x i32 vector value
def v5i32    : ValueType<160, 49>;   //  5 x i32 vector value
def v8i32    : ValueType<256, 50>;   //  8 x i32 vector value
def v16i32   : ValueType<512, 51>;   // 16 x i32 vector value
def v32i32   : ValueType<1024,52>;   // 32 x i32 vector value
def v64i32   : ValueType<2048,53>;   // 64 x i32 vector value
def v128i32  : ValueType<4096,54>;   // 128 x i32 vector value
def v256i32  : ValueType<8182,55>;   // 256 x i32 vector value
def v512i32  : ValueType<16384,56>;  // 512 x i32 vector value
def v1024i32 : ValueType<32768,57>;  // 1024 x i32 vector value
def v2048i32 : ValueType<65536,58>;  // 2048 x i32 vector value

def v1i64  : ValueType<64 , 59>;   //  1 x i64 vector value
def v2i64  : ValueType<128, 60>;   //  2 x i64 vector value
def v4i64  : ValueType<256, 61>;   //  4 x i64 vector value
def v8i64  : ValueType<512, 62>;   //  8 x i64 vector value
def v16i64 : ValueType<1024,63>;   // 16 x i64 vector value
def v32i64 : ValueType<2048,64>;   // 32 x i64 vector value

def v1i128 : ValueType<128, 65>;   //  1 x i128 vector value

def v2f16    : ValueType<32 , 66>;    //    2 x f16 vector value
def v3f16    : ValueType<48 , 67>;    //    3 x f16 vector value
def v4f16    : ValueType<64 , 68>;    //    4 x f16 vector value
def v8f16    : ValueType<128, 69>;    //    8 x f16 vector value
def v16f16   : ValueType<256, 70>;    //    8 x f16 vector value
def v32f16   : ValueType<512, 71>;    //    8 x f16 vector value
def v1f32    : ValueType<32 , 72>;    //    1 x f32 vector value
def v2f32    : ValueType<64 , 73>;    //    2 x f32 vector value
def v3f32    : ValueType<96 , 74>;    //    3 x f32 vector value
def v4f32    : ValueType<128, 75>;    //    4 x f32 vector value
def v5f32    : ValueType<160, 76>;    //    5 x f32 vector value
def v8f32    : ValueType<256, 77>;    //    8 x f32 vector value
def v16f32   : ValueType<512,  78>;   //   16 x f32 vector value
def v32f32   : ValueType<1024, 79>;   //   32 x f32 vector value
def v64f32   : ValueType<2048, 80>;   //   64 x f32 vector value
def v128f32  : ValueType<4096, 81>;   //  128 x f32 vector value
def v256f32  : ValueType<8182, 82>;   //  256 x f32 vector value
def v512f32  : ValueType<16384, 83>;  //  512 x f32 vector value
def v1024f32 : ValueType<32768, 84>;  // 1024 x f32 vector value
def v2048f32 : ValueType<65536, 85>;  // 2048 x f32 vector value
def v1f64    : ValueType<64, 86>;     //    1 x f64 vector value
def v2f64    : ValueType<128, 87>;    //    2 x f64 vector value
def v4f64    : ValueType<256, 88>;    //    4 x f64 vector value
def v8f64    : ValueType<512, 89>;    //    8 x f64 vector value

def nxv1i1  : ValueType<1,   90>;  // n x  1 x i1  vector value
def nxv2i1  : ValueType<2,   91>;  // n x  2 x i1  vector value
def nxv4i1  : ValueType<4,   92>;  // n x  4 x i1  vector value
def nxv8i1  : ValueType<8,   93>;  // n x  8 x i1  vector value
def nxv16i1 : ValueType<16,  94>;  // n x 16 x i1  vector value
def nxv32i1 : ValueType<32,  95>;  // n x 32 x i1  vector value

def nxv1i8  : ValueType<8,   96>;  // n x  1 x i8  vector value
def nxv2i8  : ValueType<16,  97>;  // n x  2 x i8  vector value
def nxv4i8  : ValueType<32,  98>;  // n x  4 x i8  vector value
def nxv8i8  : ValueType<64,  99>;  // n x  8 x i8  vector value
def nxv16i8 : ValueType<128, 100>;  // n x 16 x i8  vector value
def nxv32i8 : ValueType<256, 101>;  // n x 32 x i8  vector value

def nxv1i16 : ValueType<16,  102>; // n x  1 x i16 vector value
def nxv2i16 : ValueType<32,  103>; // n x  2 x i16 vector value
def nxv4i16 : ValueType<64,  104>; // n x  4 x i16 vector value
def nxv8i16 : ValueType<128, 105>; // n x  8 x i16 vector value
def nxv16i16: ValueType<256, 106>; // n x 16 x i16 vector value
def nxv32i16: ValueType<512, 107>; // n x 32 x i16 vector value

def nxv1i32 : ValueType<32,  108>; // n x  1 x i32 vector value
def nxv2i32 : ValueType<64,  109>; // n x  2 x i32 vector value
def nxv4i32 : ValueType<128, 110>; // n x  4 x i32 vector value
def nxv8i32 : ValueType<256, 111>; // n x  8 x i32 vector value
def nxv16i32: ValueType<512, 112>; // n x 16 x i32 vector value
def nxv32i32: ValueType<1024,113>; // n x 32 x i32 vector value

def nxv1i64 : ValueType<64,  114>; // n x  1 x i64 vector value
def nxv2i64 : ValueType<128, 115>; // n x  2 x i64 vector value
def nxv4i64 : ValueType<256, 116>; // n x  4 x i64 vector value
def nxv8i64 : ValueType<512, 117>; // n x  8 x i64 vector value
def nxv16i64: ValueType<1024,118>; // n x 16 x i64 vector value
def nxv32i64: ValueType<2048,119>; // n x 32 x i64 vector value

def nxv2f16  : ValueType<32 , 120>; // n x  2 x f16 vector value
def nxv4f16  : ValueType<64 , 121>; // n x  4 x f16 vector value
def nxv8f16  : ValueType<128, 122>; // n x  8 x f16 vector value
def nxv1f32  : ValueType<32 , 123>; // n x  1 x f32 vector value
def nxv2f32  : ValueType<64 , 124>; // n x  2 x f32 vector value
def nxv4f32  : ValueType<128, 125>; // n x  4 x f32 vector value
def nxv8f32  : ValueType<256, 126>; // n x  8 x f32 vector value
def nxv16f32 : ValueType<512, 127>; // n x 16 x f32 vector value
def nxv1f64  : ValueType<64,  128>; // n x  1 x f64 vector value
def nxv2f64  : ValueType<128, 129>; // n x  2 x f64 vector value
def nxv4f64  : ValueType<256, 130>; // n x  4 x f64 vector value
def nxv8f64  : ValueType<512, 131>; // n x  8 x f64 vector value

def x86mmx : ValueType<64 , 132>;   // X86 MMX value
def FlagVT : ValueType<0  , 133>;   // Pre-RA sched glue
def isVoid : ValueType<0  , 134>;   // Produces no value
def untyped: ValueType<8  , 135>;   // Produces an untyped value
def exnref: ValueType<0, 136>;      // WebAssembly's exnref type
def token  : ValueType<0  , 248>;   // TokenTy
def MetadataVT: ValueType<0, 249>;  // Metadata

// Pseudo valuetype mapped to the current pointer size to any address space.
// Should only be used in TableGen.
def iPTRAny   : ValueType<0, 250>;

// Pseudo valuetype to represent "vector of any size"
def vAny   : ValueType<0  , 251>;

// Pseudo valuetype to represent "float of any format"
def fAny   : ValueType<0  , 252>;

// Pseudo valuetype to represent "integer of any bit width"
def iAny   : ValueType<0  , 253>;

// Pseudo valuetype mapped to the current pointer size.
def iPTR   : ValueType<0  , 254>;

// Pseudo valuetype to represent "any type of any size".
def Any    : ValueType<0  , 255>;

/// This class is for targets that want to use pointer types in patterns
/// with the GlobalISelEmitter.  Targets must define their own pointer
/// derived from this class.  The scalar argument should be an
/// integer type with the same bit size as the ponter.
/// e.g. def p0 : PtrValueType <i64, 0>;

class PtrValueType <ValueType scalar, int addrspace> :
    ValueType<scalar.Size, scalar.Value> {
  int AddrSpace = addrspace;
}
