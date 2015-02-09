; RUN: llc < %s -march=amdgcn -mcpu=SI -verify-machineinstrs -filetype=obj | llvm-readobj -s -symbols - | FileCheck --check-prefix=ELF %s
; RUN: llc < %s -march=amdgcn -mcpu=SI -verify-machineinstrs -o - | FileCheck --check-prefix=CONFIG %s
; RUN: llc < %s -march=amdgcn -mcpu=tonga -verify-machineinstrs -filetype=obj | llvm-readobj -s -symbols - | FileCheck --check-prefix=ELF %s
; RUN: llc < %s -march=amdgcn -mcpu=tonga -verify-machineinstrs -o - | FileCheck --check-prefix=CONFIG %s

; ELF: Format: ELF32
; ELF: Name: .AMDGPU.config
; ELF: Type: SHT_PROGBITS

; ELF: Symbol {
; ELF: Name: test
; ELF: Binding: Global

; CONFIG: .align 256
; CONFIG: test:
; CONFIG: .section .AMDGPU.config
; CONFIG-NEXT: .long   45096
; CONFIG-NEXT: .long   0
define void @test(i32 %p) #0 {
   %i = add i32 %p, 2
   %r = bitcast i32 %i to float
   call void @llvm.SI.export(i32 15, i32 0, i32 1, i32 12, i32 0, float %r, float %r, float %r, float %r)
   ret void
}

declare void @llvm.SI.export(i32, i32, i32, i32, i32, float, float, float, float)

attributes #0 = { "ShaderType"="0" } ; Pixel Shader
