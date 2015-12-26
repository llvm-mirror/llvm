// RUN: not llvm-mc -arch=amdgcn %s 2>&1 | FileCheck -check-prefix=GCN %s
// RUN: not llvm-mc -arch=amdgcn -mcpu=tahiti %s 2>&1 | FileCheck -check-prefix=GCN -check-prefix=SI %s
// RUN: not llvm-mc -arch=amdgcn -mcpu=tonga %s 2>&1 | FileCheck -check-prefix=GCN -check-prefix=VI %s

s_mov_b32 v1, s2
// GCN: error: invalid operand for instruction

s_mov_b32 s1, v0
// GCN: error: invalid operand for instruction

s_mov_b32 s[1:2], s0
// GCN: error: invalid operand for instruction

s_mov_b32 s0, s[1:2]
// GCN: error: invalid operand for instruction

s_mov_b32 s220, s0
// GCN: error: invalid operand for instruction

s_mov_b32 s0, s220
// GCN: error: invalid operand for instruction

s_mov_b64 s1, s[0:1]
// GCN: error: invalid operand for instruction

s_mov_b64 s[0:1], s1
// GCN: error: invalid operand for instruction

// Immediate greater than 32-bits
s_mov_b32 s1, 0xfffffffff
// GCN: error: invalid immediate: only 32-bit values are legal

// Immediate greater than 32-bits
s_mov_b64 s[0:1], 0xfffffffff
// GCN: error: invalid immediate: only 32-bit values are legal

s_mov_b64 s[0:1], 0xfffffffff
// GCN: error: invalid immediate: only 32-bit values are legal

s_mov_b64 s[0:1], 0xfffffffff
// GCN: error: invalid immediate: only 32-bit values are legal

s_mov_b64 s[0:1], 0x0000000200000000
// GCN: error: invalid immediate: only 32-bit values are legal

// FIXME: This shoudl probably say failed to parse.
s_mov_b32 s
// GCN: error: invalid operand for instruction
// Out of range register

s_mov_b32 s102, 1
// VI: error: invalid operand for instruction
// SI-NOT: error

s_mov_b32 s103, 1
// VI: error: invalid operand for instruction
// SI-NOT: error

s_mov_b64 s[102:103], -1
// VI: error: invalid operand for instruction
// SI-NOT: error
