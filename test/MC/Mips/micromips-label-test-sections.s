# RUN: llvm-mc %s -triple=mipsel-unknown-linux -mcpu=mips32r2 \
# RUN:   -mattr=+micromips -filetype=obj -o - | llvm-readobj -t | FileCheck %s
  .text
  .set micromips
f:
  nop
g:
  .section .text
h:
  nop

# CHECK: Symbols [
# CHECK:   Symbol {
# CHECK:     Name: f
# CHECK:     Binding: Local
# CHECK:     Type: None
# CHECK:     Other: 128
# CHECK:     Section: .text
# CHECK:   }
# CHECK:   Symbol {
# CHECK:     Name: g
# CHECK:     Binding: Local
# CHECK:     Type: None
# CHECK:     Other: 0
# CHECK:     Section: .text
# CHECK:   }
# CHECK:   Symbol {
# CHECK:     Name: h
# CHECK:     Binding: Local
# CHECK:     Type: None
# CHECK:     Other: 128
# CHECK:     Section: .text
# CHECK:   }
# CHECK: ]

