// RUN: llvm-mc -triple aarch64-none-linux-gnu < %s | FileCheck %s

// Test that a single slash is not mistaken as the start of comment.

//CHECK: movz    x0, #0x10
    movz x0, #(32 / 2)
