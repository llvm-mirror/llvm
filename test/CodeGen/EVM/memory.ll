; RUN: llvm-as < %s | llc -mtriple=evm -filetype=asm | FileCheck %s


