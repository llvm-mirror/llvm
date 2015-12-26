# RUN: not llvm-mc %s -arch=mips -mcpu=mips32r6 2>&1 | \
# RUN: FileCheck %s --check-prefix=R6
# RUN: not llvm-mc %s -arch=mips64 -mcpu=mips64r6 2>&1 | \
# RUN: FileCheck %s --check-prefix=R6
# RUN: llvm-mc %s -arch=mips -mcpu=mips32r2 2>&1 | \
# RUN: FileCheck %s --check-prefix=NOT-R6
# RUN: llvm-mc %s -arch=mips64 -mcpu=mips64r2 2>&1 | \
# RUN: FileCheck %s --check-prefix=NOT-R6

  .text
  ddiv $25, $11
  # R6: :[[@LINE-1]]:3: error: instruction not supported on mips32r6 or mips64r6

  ddiv $25, $0
  # NOT-R6: :[[@LINE-1]]:3: warning: division by zero

  ddiv $0,$0
  # NOT-R6: :[[@LINE-1]]:3: warning: dividing zero by zero
