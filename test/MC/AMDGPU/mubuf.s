// RUN: not llvm-mc -arch=amdgcn -mcpu=tahiti -show-encoding %s | FileCheck -check-prefix=GCN -check-prefix=SI -check-prefix=SICI %s
// RUN: not llvm-mc -arch=amdgcn -mcpu=bonaire -show-encoding %s | FileCheck -check-prefix=GCN -check-prefix=CI -check-prefix=SICI %s

// RUN: not llvm-mc -arch=amdgcn -mcpu=tahiti %s 2>&1 | FileCheck -check-prefix=NOSI %s
// RUN: not llvm-mc -arch=amdgcn -mcpu=bonaire %s 2>&1 | FileCheck -check-prefix=NOCI %s
// RUN: not llvm-mc -arch=amdgcn -mcpu=tonga %s 2>&1 | FileCheck -check-prefix=NOVI %s

//===----------------------------------------------------------------------===//
// Test for different operand combinations
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// load - immediate offset only
//===----------------------------------------------------------------------===//

buffer_load_dword v1, s[4:7], s1
// SICI: buffer_load_dword v1, s[4:7], s1 ; encoding: [0x00,0x00,0x30,0xe0,0x00,0x01,0x01,0x01]

buffer_load_dword v1, s[4:7], s1 offset:4
// SICI: buffer_load_dword v1, s[4:7], s1 offset:4 ; encoding: [0x04,0x00,0x30,0xe0,0x00,0x01,0x01,0x01]

buffer_load_dword v1, s[4:7], s1 offset:4 glc
// SICI: buffer_load_dword v1, s[4:7], s1 offset:4 glc ; encoding: [0x04,0x40,0x30,0xe0,0x00,0x01,0x01,0x01]

buffer_load_dword v1, s[4:7], s1 offset:4 slc
// SICI: buffer_load_dword v1, s[4:7], s1 offset:4 slc ; encoding: [0x04,0x00,0x30,0xe0,0x00,0x01,0x41,0x01]

buffer_load_dword v1, s[4:7], s1 offset:4 tfe
// SICI: buffer_load_dword v1, s[4:7], s1 offset:4 tfe ; encoding: [0x04,0x00,0x30,0xe0,0x00,0x01,0x81,0x01]

buffer_load_dword v1, s[4:7], s1 tfe glc
// SICI: buffer_load_dword v1, s[4:7], s1 glc tfe ; encoding: [0x00,0x40,0x30,0xe0,0x00,0x01,0x81,0x01]

buffer_load_dword v1, s[4:7], s1 offset:4 glc tfe slc
// SICI: buffer_load_dword v1, s[4:7], s1 offset:4 glc slc tfe ; encoding: [0x04,0x40,0x30,0xe0,0x00,0x01,0xc1,0x01]

buffer_load_dword v1, s[4:7], s1 glc tfe slc offset:4
// SICI: buffer_load_dword v1, s[4:7], s1 offset:4 glc slc tfe ; encoding: [0x04,0x40,0x30,0xe0,0x00,0x01,0xc1,0x01]

//===----------------------------------------------------------------------===//
// load - vgpr offset
//===----------------------------------------------------------------------===//

buffer_load_dword v1, v2, s[4:7], s1 offen
// SICI: buffer_load_dword v1, v2, s[4:7], s1 offen ; encoding: [0x00,0x10,0x30,0xe0,0x02,0x01,0x01,0x01]

buffer_load_dword v1, v2, s[4:7], s1 offen offset:4
// SICI: buffer_load_dword v1, v2, s[4:7], s1 offen offset:4 ; encoding: [0x04,0x10,0x30,0xe0,0x02,0x01,0x01,0x01]

buffer_load_dword v1, v2, s[4:7], s1 offen offset:4 glc
// SICI: buffer_load_dword v1, v2, s[4:7], s1 offen  offset:4 glc ; encoding: [0x04,0x50,0x30,0xe0,0x02,0x01,0x01,0x01]

buffer_load_dword v1, v2, s[4:7], s1 offen offset:4 slc
// SICI: buffer_load_dword v1, v2, s[4:7], s1 offen offset:4 slc ; encoding: [0x04,0x10,0x30,0xe0,0x02,0x01,0x41,0x01]

buffer_load_dword v1, v2, s[4:7], s1 offen offset:4 tfe
// SICI: buffer_load_dword v1, v2, s[4:7], s1 offen offset:4 tfe ; encoding: [0x04,0x10,0x30,0xe0,0x02,0x01,0x81,0x01]

buffer_load_dword v1, v2, s[4:7], s1 offen tfe glc
// SICI: buffer_load_dword v1, v2, s[4:7], s1 offen glc tfe ; encoding: [0x00,0x50,0x30,0xe0,0x02,0x01,0x81,0x01]

buffer_load_dword v1, v2, s[4:7], s1 offen offset:4 glc tfe slc
// SICI: buffer_load_dword v1, v2, s[4:7], s1 offen offset:4 glc slc tfe ; encoding: [0x04,0x50,0x30,0xe0,0x02,0x01,0xc1,0x01]

buffer_load_dword v1, v2, s[4:7], s1 offen glc tfe slc offset:4
// SICI: buffer_load_dword v1, v2, s[4:7], s1 offen offset:4 glc slc tfe ; encoding: [0x04,0x50,0x30,0xe0,0x02,0x01,0xc1,0x01]

//===----------------------------------------------------------------------===//
// load - vgpr index
//===----------------------------------------------------------------------===//

buffer_load_dword v1, v2, s[4:7], s1 idxen
// SICI: buffer_load_dword v1, v2, s[4:7], s1 idxen ; encoding: [0x00,0x20,0x30,0xe0,0x02,0x01,0x01,0x01]

buffer_load_dword v1, v2, s[4:7], s1 idxen offset:4
// SICI: buffer_load_dword v1, v2, s[4:7], s1 idxen offset:4 ; encoding: [0x04,0x20,0x30,0xe0,0x02,0x01,0x01,0x01]

buffer_load_dword v1, v2, s[4:7], s1 idxen offset:4 glc
// SICI: buffer_load_dword v1, v2, s[4:7], s1 idxen offset:4 glc ; encoding: [0x04,0x60,0x30,0xe0,0x02,0x01,0x01,0x01]

buffer_load_dword v1, v2, s[4:7], s1 idxen offset:4 slc
// SICI: buffer_load_dword v1, v2, s[4:7], s1 idxen offset:4 slc ; encoding: [0x04,0x20,0x30,0xe0,0x02,0x01,0x41,0x01]

buffer_load_dword v1, v2, s[4:7], s1 idxen offset:4 tfe
// SICI: buffer_load_dword v1, v2, s[4:7], s1 idxen offset:4 tfe ; encoding: [0x04,0x20,0x30,0xe0,0x02,0x01,0x81,0x01]

buffer_load_dword v1, v2, s[4:7], s1 idxen tfe glc
// SICI: buffer_load_dword v1, v2, s[4:7], s1 idxen glc tfe ; encoding: [0x00,0x60,0x30,0xe0,0x02,0x01,0x81,0x01]

buffer_load_dword v1, v2, s[4:7], s1 idxen offset:4 glc tfe slc
// SICI: buffer_load_dword v1, v2, s[4:7], s1 idxen offset:4 glc slc tfe ; encoding: [0x04,0x60,0x30,0xe0,0x02,0x01,0xc1,0x01]

buffer_load_dword v1, v2, s[4:7], s1 idxen glc tfe slc offset:4
// SICI: buffer_load_dword v1, v2, s[4:7], s1 idxen offset:4 glc slc tfe ; encoding: [0x04,0x60,0x30,0xe0,0x02,0x01,0xc1,0x01]

//===----------------------------------------------------------------------===//
// load - vgpr index and offset
//===----------------------------------------------------------------------===//

buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen ; encoding: [0x00,0x30,0x30,0xe0,0x02,0x01,0x01,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 ; encoding: [0x04,0x30,0x30,0xe0,0x02,0x01,0x01,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 glc
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 glc ; encoding: [0x04,0x70,0x30,0xe0,0x02,0x01,0x01,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 slc
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 slc ; encoding: [0x04,0x30,0x30,0xe0,0x02,0x01,0x41,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 tfe
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 tfe ; encoding: [0x04,0x30,0x30,0xe0,0x02,0x01,0x81,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen tfe glc
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen glc tfe ; encoding: [0x00,0x70,0x30,0xe0,0x02,0x01,0x81,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 glc tfe slc
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 glc slc tfe ; encoding: [0x04,0x70,0x30,0xe0,0x02,0x01,0xc1,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen glc tfe slc offset:4
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 glc slc tfe ; encoding: [0x04,0x70,0x30,0xe0,0x02,0x01,0xc1,0x01]

//===----------------------------------------------------------------------===//
// load - addr64
//===----------------------------------------------------------------------===//

buffer_load_dword v1, v[2:3], s[4:7], s1 addr64
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 ; encoding: [0x00,0x80,0x30,0xe0,0x02,0x01,0x01,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 offset:4
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 ; encoding: [0x04,0x80,0x30,0xe0,0x02,0x01,0x01,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 glc
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 glc ; encoding: [0x04,0xc0,0x30,0xe0,0x02,0x01,0x01,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 slc
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 slc ; encoding: [0x04,0x80,0x30,0xe0,0x02,0x01,0x41,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 tfe
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 tfe ; encoding: [0x04,0x80,0x30,0xe0,0x02,0x01,0x81,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 tfe glc
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 glc tfe ; encoding: [0x00,0xc0,0x30,0xe0,0x02,0x01,0x81,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 glc tfe slc
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 glc slc tfe ; encoding: [0x04,0xc0,0x30,0xe0,0x02,0x01,0xc1,0x01]

buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 glc tfe slc offset:4
// SICI: buffer_load_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 glc slc tfe ; encoding: [0x04,0xc0,0x30,0xe0,0x02,0x01,0xc1,0x01]

//===----------------------------------------------------------------------===//
// store - immediate offset only
//===----------------------------------------------------------------------===//

buffer_store_dword v1, s[4:7], s1
// SICI: buffer_store_dword v1, s[4:7], s1 ; encoding: [0x00,0x00,0x70,0xe0,0x00,0x01,0x01,0x01]

buffer_store_dword v1, s[4:7], s1 offset:4
// SICI: buffer_store_dword v1, s[4:7], s1 offset:4 ; encoding: [0x04,0x00,0x70,0xe0,0x00,0x01,0x01,0x01]

buffer_store_dword v1, s[4:7], s1 offset:4 glc
// SICI: buffer_store_dword v1, s[4:7], s1 offset:4 glc ; encoding: [0x04,0x40,0x70,0xe0,0x00,0x01,0x01,0x01]

buffer_store_dword v1, s[4:7], s1 offset:4 slc
// SICI: buffer_store_dword v1, s[4:7], s1 offset:4 slc ; encoding: [0x04,0x00,0x70,0xe0,0x00,0x01,0x41,0x01]

buffer_store_dword v1, s[4:7], s1 offset:4 tfe
// SICI: buffer_store_dword v1, s[4:7], s1 offset:4 tfe ; encoding: [0x04,0x00,0x70,0xe0,0x00,0x01,0x81,0x01]

buffer_store_dword v1, s[4:7], s1 tfe glc
// SICI: buffer_store_dword v1, s[4:7], s1 glc tfe ; encoding: [0x00,0x40,0x70,0xe0,0x00,0x01,0x81,0x01]

buffer_store_dword v1, s[4:7], s1 offset:4 glc tfe slc
// SICI: buffer_store_dword v1, s[4:7], s1 offset:4 glc slc tfe ; encoding: [0x04,0x40,0x70,0xe0,0x00,0x01,0xc1,0x01]

buffer_store_dword v1, s[4:7], s1 glc tfe slc offset:4
// SICI: buffer_store_dword v1, s[4:7], s1 offset:4 glc slc tfe ; encoding: [0x04,0x40,0x70,0xe0,0x00,0x01,0xc1,0x01]

//===----------------------------------------------------------------------===//
// store - vgpr offset
//===----------------------------------------------------------------------===//

buffer_store_dword v1, v2, s[4:7], s1 offen
// SICI: buffer_store_dword v1, v2, s[4:7], s1 offen ; encoding: [0x00,0x10,0x70,0xe0,0x02,0x01,0x01,0x01]

buffer_store_dword v1, v2, s[4:7], s1 offen offset:4
// SICI: buffer_store_dword v1, v2, s[4:7], s1 offen offset:4 ; encoding: [0x04,0x10,0x70,0xe0,0x02,0x01,0x01,0x01]

buffer_store_dword v1, v2, s[4:7], s1 offen offset:4 glc
// SICI: buffer_store_dword v1, v2, s[4:7], s1 offen  offset:4 glc ; encoding: [0x04,0x50,0x70,0xe0,0x02,0x01,0x01,0x01]

buffer_store_dword v1, v2, s[4:7], s1 offen offset:4 slc
// SICI: buffer_store_dword v1, v2, s[4:7], s1 offen offset:4 slc ; encoding: [0x04,0x10,0x70,0xe0,0x02,0x01,0x41,0x01]

buffer_store_dword v1, v2, s[4:7], s1 offen offset:4 tfe
// SICI: buffer_store_dword v1, v2, s[4:7], s1 offen offset:4 tfe ; encoding: [0x04,0x10,0x70,0xe0,0x02,0x01,0x81,0x01]

buffer_store_dword v1, v2, s[4:7], s1 offen tfe glc
// SICI: buffer_store_dword v1, v2, s[4:7], s1 offen glc tfe ; encoding: [0x00,0x50,0x70,0xe0,0x02,0x01,0x81,0x01]

buffer_store_dword v1, v2, s[4:7], s1 offen offset:4 glc tfe slc
// SICI: buffer_store_dword v1, v2, s[4:7], s1 offen offset:4 glc slc tfe ; encoding: [0x04,0x50,0x70,0xe0,0x02,0x01,0xc1,0x01]

buffer_store_dword v1, v2, s[4:7], s1 offen glc tfe slc offset:4
// SICI: buffer_store_dword v1, v2, s[4:7], s1 offen offset:4 glc slc tfe ; encoding: [0x04,0x50,0x70,0xe0,0x02,0x01,0xc1,0x01]

//===----------------------------------------------------------------------===//
// store - vgpr index
//===----------------------------------------------------------------------===//

buffer_store_dword v1, v2, s[4:7], s1 idxen
// SICI: buffer_store_dword v1, v2, s[4:7], s1 idxen ; encoding: [0x00,0x20,0x70,0xe0,0x02,0x01,0x01,0x01]

buffer_store_dword v1, v2, s[4:7], s1 idxen offset:4
// SICI: buffer_store_dword v1, v2, s[4:7], s1 idxen offset:4 ; encoding: [0x04,0x20,0x70,0xe0,0x02,0x01,0x01,0x01]

buffer_store_dword v1, v2, s[4:7], s1 idxen offset:4 glc
// SICI: buffer_store_dword v1, v2, s[4:7], s1 idxen offset:4 glc ; encoding: [0x04,0x60,0x70,0xe0,0x02,0x01,0x01,0x01]

buffer_store_dword v1, v2, s[4:7], s1 idxen offset:4 slc
// SICI: buffer_store_dword v1, v2, s[4:7], s1 idxen offset:4 slc ; encoding: [0x04,0x20,0x70,0xe0,0x02,0x01,0x41,0x01]

buffer_store_dword v1, v2, s[4:7], s1 idxen offset:4 tfe
// SICI: buffer_store_dword v1, v2, s[4:7], s1 idxen offset:4 tfe ; encoding: [0x04,0x20,0x70,0xe0,0x02,0x01,0x81,0x01]

buffer_store_dword v1, v2, s[4:7], s1 idxen tfe glc
// SICI: buffer_store_dword v1, v2, s[4:7], s1 idxen glc tfe ; encoding: [0x00,0x60,0x70,0xe0,0x02,0x01,0x81,0x01]

buffer_store_dword v1, v2, s[4:7], s1 idxen offset:4 glc tfe slc
// SICI: buffer_store_dword v1, v2, s[4:7], s1 idxen offset:4 glc slc tfe ; encoding: [0x04,0x60,0x70,0xe0,0x02,0x01,0xc1,0x01]

buffer_store_dword v1, v2, s[4:7], s1 idxen glc tfe slc offset:4
// SICI: buffer_store_dword v1, v2, s[4:7], s1 idxen offset:4 glc slc tfe ; encoding: [0x04,0x60,0x70,0xe0,0x02,0x01,0xc1,0x01]

//===----------------------------------------------------------------------===//
// store - vgpr index and offset
//===----------------------------------------------------------------------===//

buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen ; encoding: [0x00,0x30,0x70,0xe0,0x02,0x01,0x01,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 ; encoding: [0x04,0x30,0x70,0xe0,0x02,0x01,0x01,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 glc
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 glc ; encoding: [0x04,0x70,0x70,0xe0,0x02,0x01,0x01,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 slc
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 slc ; encoding: [0x04,0x30,0x70,0xe0,0x02,0x01,0x41,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 tfe
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 tfe ; encoding: [0x04,0x30,0x70,0xe0,0x02,0x01,0x81,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen tfe glc
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen glc tfe ; encoding: [0x00,0x70,0x70,0xe0,0x02,0x01,0x81,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 glc tfe slc
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 glc slc tfe ; encoding: [0x04,0x70,0x70,0xe0,0x02,0x01,0xc1,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen glc tfe slc offset:4
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 idxen offen offset:4 glc slc tfe ; encoding: [0x04,0x70,0x70,0xe0,0x02,0x01,0xc1,0x01]

//===----------------------------------------------------------------------===//
// store - addr64
//===----------------------------------------------------------------------===//

buffer_store_dword v1, v[2:3], s[4:7], s1 addr64
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 ; encoding: [0x00,0x80,0x70,0xe0,0x02,0x01,0x01,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 offset:4
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 ; encoding: [0x04,0x80,0x70,0xe0,0x02,0x01,0x01,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 glc
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 glc ; encoding: [0x04,0xc0,0x70,0xe0,0x02,0x01,0x01,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 slc
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 slc ; encoding: [0x04,0x80,0x70,0xe0,0x02,0x01,0x41,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 tfe
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 tfe ; encoding: [0x04,0x80,0x70,0xe0,0x02,0x01,0x81,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 tfe glc
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 glc tfe ; encoding: [0x00,0xc0,0x70,0xe0,0x02,0x01,0x81,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 glc tfe slc
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 glc slc tfe ; encoding: [0x04,0xc0,0x70,0xe0,0x02,0x01,0xc1,0x01]

buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 glc tfe slc offset:4
// SICI: buffer_store_dword v1, v[2:3], s[4:7], s1 addr64 offset:4 glc slc tfe ; encoding: [0x04,0xc0,0x70,0xe0,0x02,0x01,0xc1,0x01]

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

buffer_load_format_x v1, s[4:7], s1
// SICI: buffer_load_format_x v1, s[4:7], s1 ; encoding: [0x00,0x00,0x00,0xe0,0x00,0x01,0x01,0x01]

buffer_load_format_xy v[1:2], s[4:7], s1
// SICI: buffer_load_format_xy v[1:2], s[4:7], s1 ; encoding: [0x00,0x00,0x04,0xe0,0x00,0x01,0x01,0x01]

buffer_load_format_xyz v[1:3], s[4:7], s1
// SICI: buffer_load_format_xyz v[1:3], s[4:7], s1 ; encoding: [0x00,0x00,0x08,0xe0,0x00,0x01,0x01,0x01]

buffer_load_format_xyzw v[1:4], s[4:7], s1
// SICI: buffer_load_format_xyzw v[1:4], s[4:7], s1 ; encoding: [0x00,0x00,0x0c,0xe0,0x00,0x01,0x01,0x01]

buffer_store_format_x v1, s[4:7], s1
// SICI: buffer_store_format_x v1, s[4:7], s1 ; encoding: [0x00,0x00,0x10,0xe0,0x00,0x01,0x01,0x01]

buffer_store_format_xy v[1:2], s[4:7], s1
// SICI: buffer_store_format_xy v[1:2], s[4:7], s1 ; encoding: [0x00,0x00,0x14,0xe0,0x00,0x01,0x01,0x01]

buffer_store_format_xyz v[1:3], s[4:7], s1
// SICI: buffer_store_format_xyz v[1:3], s[4:7], s1 ; encoding: [0x00,0x00,0x18,0xe0,0x00,0x01,0x01,0x01]

buffer_store_format_xyzw v[1:4], s[4:7], s1
// SICI: buffer_store_format_xyzw v[1:4], s[4:7], s1 ; encoding: [0x00,0x00,0x1c,0xe0,0x00,0x01,0x01,0x01]

buffer_load_ubyte v1, s[4:7], s1
// SICI: buffer_load_ubyte v1, s[4:7], s1 ; encoding: [0x00,0x00,0x20,0xe0,0x00,0x01,0x01,0x01]

buffer_load_sbyte v1, s[4:7], s1
// SICI: buffer_load_sbyte v1, s[4:7], s1 ; encoding: [0x00,0x00,0x24,0xe0,0x00,0x01,0x01,0x01]

buffer_load_ushort v1, s[4:7], s1
// SICI: buffer_load_ushort v1, s[4:7], s1 ; encoding: [0x00,0x00,0x28,0xe0,0x00,0x01,0x01,0x01]

buffer_load_sshort v1, s[4:7], s1
// SICI: buffer_load_sshort v1, s[4:7], s1 ; encoding: [0x00,0x00,0x2c,0xe0,0x00,0x01,0x01,0x01]

buffer_load_dword v1, s[4:7], s1
// SICI: buffer_load_dword v1, s[4:7], s1 ; encoding: [0x00,0x00,0x30,0xe0,0x00,0x01,0x01,0x01]

buffer_load_dwordx2 v[1:2], s[4:7], s1
// SICI: buffer_load_dwordx2 v[1:2], s[4:7], s1 ; encoding: [0x00,0x00,0x34,0xe0,0x00,0x01,0x01,0x01]

buffer_load_dwordx4 v[1:4], s[4:7], s1
// SICI: buffer_load_dwordx4 v[1:4], s[4:7], s1 ; encoding: [0x00,0x00,0x38,0xe0,0x00,0x01,0x01,0x01]

buffer_store_byte v1, s[4:7], s1
// SICI: buffer_store_byte v1, s[4:7], s1 ; encoding: [0x00,0x00,0x60,0xe0,0x00,0x01,0x01,0x01]

buffer_store_short v1, s[4:7], s1
// SICI: buffer_store_short v1, s[4:7], s1 ; encoding: [0x00,0x00,0x68,0xe0,0x00,0x01,0x01,0x01]

buffer_store_dword v1 s[4:7], s1
// SICI: buffer_store_dword v1, s[4:7], s1 ; encoding: [0x00,0x00,0x70,0xe0,0x00,0x01,0x01,0x01]

buffer_store_dwordx2 v[1:2], s[4:7], s1
// SICI: buffer_store_dwordx2 v[1:2], s[4:7], s1 ; encoding: [0x00,0x00,0x74,0xe0,0x00,0x01,0x01,0x01]

buffer_store_dwordx4 v[1:4], s[4:7], s1
// SICI: buffer_store_dwordx4 v[1:4], s[4:7], s1 ; encoding: [0x00,0x00,0x78,0xe0,0x00,0x01,0x01,0x01]

//===----------------------------------------------------------------------===//
// Cache invalidation
//===----------------------------------------------------------------------===//

buffer_wbinvl1
// SICI: buffer_wbinvl1   ; encoding: [0x00,0x00,0xc4,0xe1,0x00,0x00,0x00,0x00]

buffer_wbinvl1_sc
// SI: buffer_wbinvl1_sc ; encoding: [0x00,0x00,0xc0,0xe1,0x00,0x00,0x00,0x00]
// NOCI: error: instruction not supported on this GPU
// NOVI: error: instruction not supported on this GPU

buffer_wbinvl1_vol
// CI: buffer_wbinvl1_vol ; encoding: [0x00,0x00,0xc0,0xe1,0x00,0x00,0x00,0x00]
// NOSI: error: instruction not supported on this GPU

// TODO: Atomics
