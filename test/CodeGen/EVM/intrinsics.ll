; RUN: llvm-as < %s | llc -mtriple=evm -filetype=asm | FileCheck %s

define i256 @int_exp(i256 %a, i256 %b) {
entry:
  %rv = call i256 @llvm.evm.exp(i256 %a, i256 %b)
; CHECK-LABEL: int_exp
; CHECK: EXP
  ret i256 %rv
}

define i256 @int_sha3(i256 %a, i256 %b, i256 %c) {
entry:
  %rv = call i256 @llvm.evm.sha3(i256 %a, i256 %b, i256 %c)
; CHECK-LABEL: int_sha3
; CHECK: SHA3
  ret i256 %rv
}

define i256 @int_balance(i256 %addr) {
; CHECK-LABEL: int_balance
entry:
  %rv = call i256 @llvm.evm.balance(i256 %addr)
; CHECK: BALANCE
  ret i256 %rv
}



define i256 @int_origin() {
entry:
  %rv = call i256 @llvm.evm.origin()
; CHECK-LABEL: int_origin
; CHECK: ORIGIN
  ret i256 %rv
}

define i256 @int_caller() {
entry:
  %rv = call i256 @llvm.evm.caller()
; CHECK-LABEL: int_caller
; CHECK: CALLER
  ret i256 %rv
}



define i256 @int_callvalue() {
entry:
  %rv = call i256 @llvm.evm.callvalue()
; CHECK-LABEL: int_callvalue
; CHECK: CALLVALUE
  ret i256 %rv
}

define i256 @int_calldataload(i256 %addr) {
entry:
  %rv = call i256 @llvm.evm.calldataload(i256 %addr)
; CHECK-LABEL: int_calldataload
; CHECK: CALLDATALOAD
  ret i256 %rv
}

define i256 @int_calldatasize() {
entry:
  %rv = call i256 @llvm.evm.calldatasize()
; CHECK-LABEL: int_calldatasize
; CHECK: CALLDATASIZE
  ret i256 %rv
}

define void @int_calldatacopy(i256 %destoffset, i256 %offset, i256 %len) {
entry:
  call void @llvm.evm.calldatacopy(i256 %destoffset, i256 %offset, i256 %len)
; CHECK-LABEL: int_calldatacopy
; CHECK: CALLDATACOPY
  ret void
}

define i256 @int_codesize() {
entry:
  %rv = call i256 @llvm.evm.codesize()
; CHECK-LABEL: int_codesize
; CHECK: CODESIZE
  ret i256 %rv
}

define void @int_codecopy(i256 %destoffset, i256 %offset, i256 %len) {
entry:
  call void @llvm.evm.codecopy(i256 %destoffset, i256 %offset, i256 %len)
; CHECK-LABEL: int_codecopy
; CHECK: CODECOPY
  ret void
}

define i256 @int_gasprice() {
entry:
  %rv = call i256 @llvm.evm.gasprice()
; CHECK-LABEL: int_gasprice
; CHECK: GASPRICE
  ret i256 %rv
}

define i256 @int_extcodesize(i256 %addr) {
entry:
  %rv = call i256 @llvm.evm.extcodesize(i256 %addr)
; CHECK-LABEL: int_extcodesize
; CHECK: EXTCODESIZE
  ret i256 %rv
}

define void @int_extcodecopy(i256 %destoffset, i256 %offset, i256 %len) {
entry:
  call void @llvm.evm.extcodecopy(i256 %destoffset, i256 %offset, i256 %len)
; CHECK-LABEL: int_extcodecopy
; CHECK: EXTCODECOPY
  ret void
}

define i256 @int_returndatasize() {
entry:
  %rv = call i256 @llvm.evm.returndatasize()
; CHECK-LABEL: int_returndatasize
; CHECK: RETURNDATASIZE
  ret i256 %rv
}

define void @int_returndatacopy(i256 %destoffset, i256 %offset, i256 %len) {
entry:
  call void @llvm.evm.returndatacopy(i256 %destoffset, i256 %offset, i256 %len)
; CHECK-LABEL: int_returndatacopy
; CHECK: RETURNDATACOPY
  ret void
}

define i256 @int_blockhash(i256 %addr) {
entry:
  %rv = call i256 @llvm.evm.blockhash(i256 %addr)
; CHECK-LABEL: int_blockhash
; CHECK: BLOCKHASH
  ret i256 %rv
}

define i256 @int_coinbase() {
entry:
  %rv = call i256 @llvm.evm.coinbase()
; CHECK-LABEL: int_coinbase
; CHECK: COINBASE
  ret i256 %rv
}

define i256 @int_timestamp() {
entry:
  %rv = call i256 @llvm.evm.timestamp()
; CHECK-LABEL: int_timestamp
; CHECK: TIMESTAMP
  ret i256 %rv
}

define i256 @int_number() {
entry:
  %rv = call i256 @llvm.evm.number()
; CHECK-LABEL: int_number
; CHECK: NUMBER
  ret i256 %rv
}

define i256 @int_difficulty() {
entry:
  %rv = call i256 @llvm.evm.difficulty()
; CHECK-LABEL: int_difficulty
; CHECK: DIFFICULTY
  ret i256 %rv
}

define i256 @int_gaslimit() {
entry:
  %rv = call i256 @llvm.evm.gaslimit()
; CHECK-LABEL: int_gaslimit
; CHECK: GASLIMIT
  ret i256 %rv
}

define i256 @int_sload(i256 %addr) {
entry:
  %rv = call i256 @llvm.evm.sload(i256 %addr)
; CHECK-LABEL: int_sload
; CHECK: SLOAD
  ret i256 %rv
}

define void @int_sstore(i256 %addr, i256 %val) {
entry:
  %rv = call i256 @llvm.evm.sstore(i256 %addr, i256 %val)
; CHECK-LABEL: int_sstore
; CHECK: SSTORE
  ret void
}

define i256 @int_getpc() {
entry:
  %rv = call i256 @llvm.evm.getpc()
; CHECK-LABEL: int_getpc
; CHECK: GETPC
  ret i256 %rv
}

define i256 @int_msize() {
entry:
  %rv = call i256 @llvm.evm.msize()
; CHECK-LABEL: int_msize
; CHECK: MSIZE
  ret i256 %rv
}

define i256 @int_gas() {
entry:
  %rv = call i256 @llvm.evm.gas()
; CHECK-LABEL: int_gas
; CHECK: GAS
  ret i256 %rv
}

define void @int_log0(i256 %a, i256 %b) {
; CHECK-LABEL: int_log0
entry:
  call void @llvm.evm.log0(i256 %a, i256 %b)
; CHECK: log0
  ret void
}

