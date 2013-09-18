// RUN: llvm-mc -filetype=obj -triple i686-pc-win32 %s -o - | llvm-readobj -t | FileCheck %s

"@feat.00" = 123

// CHECK: Symbol {
// CHECK:   Name: @feat.00
// CHECK:   Value: 123
// CHECK:   Section: (-1)
// CHECK:   BaseType: Null (0x0)
// CHECK:   ComplexType: Null (0x0)
// CHECK:   StorageClass: External (0x2)
// CHECK:   AuxSymbolCount: 0
// CHECK: }
