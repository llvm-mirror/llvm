
# RUN: llvm-mc -triple powerpc64-unknown-unknown -filetype=obj %s | \
# RUN: llvm-readobj -s -sd | FileCheck %s

# This checks that fixups that can be resolved within the same
# object file are applied correctly.

.text

addi 1, 1, target
addis 1, 1, target

.set target, 0x1234

addi 1, 1, target2@l
addis 1, 1, target2@ha

.set target2, 0x12345678

addi 1, 1, target3-target4@l
addis 1, 1, target3-target4@ha

.set target3, 0x23455678
.set target4, 0x12341234

addi 1, 1, target5+0x8000@l
addis 1, 1, target5+0x8000@ha

.set target5, 0x10000001

1:
addi 1, 1, 2f-1b@l
addis 1, 1, 1b-2f@ha
2:

addi 1, 1, target6@h
addis 1, 1, target6@h

.set target6, 0x4321fedc

addi 1, 1, target7@higher
addis 1, 1, target7@highest
addi 1, 1, target7@highera
addis 1, 1, target7@highesta

.set target7, 0x1234ffffffff8000

.data

.quad v1
.long v2
.short v3
.byte v4

.set v1, 0x123456789abcdef0
.set v2, 0x87654321
.set v3, 0xbeef
.set v4, 0x42

# CHECK:       Section {
# CHECK:         Name: .text
# CHECK-NEXT:    Type: SHT_PROGBITS
# CHECK-NEXT:    Flags [
# CHECK-NEXT:      SHF_ALLOC
# CHECK-NEXT:      SHF_EXECINSTR
# CHECK-NEXT:    ]
# CHECK-NEXT:    Address: 0x0
# CHECK-NEXT:    Offset:
# CHECK-NEXT:    Size: 64
# CHECK-NEXT:    Link: 0
# CHECK-NEXT:    Info: 0
# CHECK-NEXT:    AddressAlignment: 4
# CHECK-NEXT:    EntrySize: 0
# CHECK-NEXT:    SectionData (
# CHECK-NEXT:      0000: 38211234 3C211234 38215678 3C211234
# CHECK-NEXT:      0010: 38214444 3C211111 38218001 3C211001
# CHECK-NEXT:      0020: 38210008 3C210000 38214321 3C214321
# CHECK-NEXT:      0030: 3821FFFF 3C211234 38210000 3C211235
# CHECK-NEXT:    )
# CHECK-NEXT:  }

# CHECK:        Section {
# CHECK:          Name: .data
# CHECK-NEXT:     Type: SHT_PROGBITS
# CHECK-NEXT:     Flags [
# CHECK-NEXT:       SHF_ALLOC
# CHECK-NEXT:       SHF_WRITE
# CHECK-NEXT:     ]
# CHECK-NEXT:     Address: 0x0
# CHECK-NEXT:     Offset:
# CHECK-NEXT:     Size: 15
# CHECK-NEXT:     Link: 0
# CHECK-NEXT:     Info: 0
# CHECK-NEXT:     AddressAlignment: 4
# CHECK-NEXT:     EntrySize: 0
# CHECK-NEXT:     SectionData (
# CHECK-NEXT:       0000: 12345678 9ABCDEF0 87654321 BEEF42
# CHECK-NEXT:     )
# CHECK-NEXT:   }

