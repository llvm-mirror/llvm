//===-- EVMRegisterInfo.td - EVM Register defs --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the EVM register files
//  The content is copied from WebAssembly backend
//===----------------------------------------------------------------------===//

class EVMReg<string n> : Register<n> {
  let Namespace = "EVM";
}

class EVMRegClass<list<ValueType> regTypes, int alignment, dag regList>
     : RegisterClass<"EVM", regTypes, alignment, regList>;

//===----------------------------------------------------------------------===//
// Registers
//===----------------------------------------------------------------------===//

// Special registers used as the frame and stack pointer.

def FP : EVMReg<"%FP">;
def SP : EVMReg<"%SP">;

// The register allocation framework requires register classes have at least
// one register, so we define a few for the integer / floating point register
// classes since we otherwise don't need a physical register in those classes.
// These are also used a "types" in the generated assembly matcher.
def INTREG : EVMReg<"%intreg">;

// The register classes.
def I256 : EVMRegClass<[i256], 32, (add FP, SP, INTREG)>;
def ADDR : EVMRegClass<[i160], 32, (add FP, SP, INTREG)>;

// The value stack "register". This is an opaque entity which serves to order
// uses and defs that must remain in LIFO order.
def REG_STACK : WebAssemblyReg<"Stack_Reg">;

// The incoming arguments "register". This is an opaque entity which serves to
// order the ARGUMENT instructions that are emulating live-in registers and
// must not be scheduled below other instructions.
def REG_ARGS : WebAssemblyReg<"Arg_Reg">;

