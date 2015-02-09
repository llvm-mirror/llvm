! RUN: llvm-mc %s -arch=sparc   -show-encoding | FileCheck %s
! RUN: llvm-mc %s -arch=sparcv9 -show-encoding | FileCheck %s

        ! CHECK: ldsb [%i0+%l6], %o2  ! encoding: [0xd4,0x4e,0x00,0x16]
        ldsb [%i0 + %l6], %o2
        ! CHECK: ldsb [%i0+32], %o2   ! encoding: [0xd4,0x4e,0x20,0x20]
        ldsb [%i0 + 32], %o2
        ! CHECK: ldsb [%g1], %o4      ! encoding: [0xd8,0x48,0x60,0x00]
        ldsb [%g1], %o4

        ! CHECK: ldsh [%i0+%l6], %o2  ! encoding: [0xd4,0x56,0x00,0x16]
        ldsh [%i0 + %l6], %o2
        ! CHECK: ldsh [%i0+32], %o2   ! encoding: [0xd4,0x56,0x20,0x20]
        ldsh [%i0 + 32], %o2
        ! CHECK: ldsh [%g1], %o4      ! encoding: [0xd8,0x50,0x60,0x00]
        ldsh [%g1], %o4

        ! CHECK: ldub [%i0+%l6], %o2  ! encoding: [0xd4,0x0e,0x00,0x16]
        ldub [%i0 + %l6], %o2
        ! CHECK: ldub [%i0+32], %o2   ! encoding: [0xd4,0x0e,0x20,0x20]
        ldub [%i0 + 32], %o2
        ! CHECK: ldub [%g1], %o2      ! encoding: [0xd4,0x08,0x60,0x00]
        ldub [%g1], %o2

        ! CHECK: lduh [%i0+%l6], %o2  ! encoding: [0xd4,0x16,0x00,0x16]
        lduh [%i0 + %l6], %o2
        ! CHECK: lduh [%i0+32], %o2   ! encoding: [0xd4,0x16,0x20,0x20]
        lduh [%i0 + 32], %o2
        ! CHECK: lduh [%g1], %o2      ! encoding: [0xd4,0x10,0x60,0x00]
        lduh [%g1], %o2

        ! CHECK: ld [%i0+%l6], %o2    ! encoding: [0xd4,0x06,0x00,0x16]
        ld [%i0 + %l6], %o2
        ! CHECK: ld [%i0+32], %o2     ! encoding: [0xd4,0x06,0x20,0x20]
        ld [%i0 + 32], %o2
        ! CHECK: ld [%g1], %o2        ! encoding: [0xd4,0x00,0x60,0x00]
        ld [%g1], %o2

        ! CHECK: stb %o2, [%i0+%l6]   ! encoding: [0xd4,0x2e,0x00,0x16]
        stb %o2, [%i0 + %l6]
        ! CHECK: stb %o2, [%i0+32]    ! encoding: [0xd4,0x2e,0x20,0x20]
        stb %o2, [%i0 + 32]
        ! CHECK: stb %o2, [%g1]       ! encoding: [0xd4,0x28,0x60,0x00]
        stb %o2, [%g1]

        ! CHECK: sth %o2, [%i0+%l6]   ! encoding: [0xd4,0x36,0x00,0x16]
        sth %o2, [%i0 + %l6]
        ! CHECK: sth %o2, [%i0+32]    ! encoding: [0xd4,0x36,0x20,0x20]
        sth %o2, [%i0 + 32]
        ! CHECK: sth %o2, [%g1]       ! encoding: [0xd4,0x30,0x60,0x00]
        sth %o2, [%g1]

        ! CHECK: st %o2, [%i0+%l6]    ! encoding: [0xd4,0x26,0x00,0x16]
        st %o2, [%i0 + %l6]
        ! CHECK: st %o2, [%i0+32]     ! encoding: [0xd4,0x26,0x20,0x20]
        st %o2, [%i0 + 32]
        ! CHECK: st %o2, [%g1]        ! encoding: [0xd4,0x20,0x60,0x00]
        st %o2, [%g1]
