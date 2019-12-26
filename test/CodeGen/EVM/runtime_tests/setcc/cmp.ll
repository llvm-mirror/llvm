declare i256 @llvm.evm.calldataload(i256)
declare void @llvm.evm.return(i256, i256)
declare void @llvm.evm.mstore(i256, i256)

define void @main() {
entry:
  %0 = call i256 @llvm.evm.calldataload(i256 0)
  %1 = call i256 @llvm.evm.calldataload(i256 32)
  %2 = call i256 @foo_cmp1(i256 %0, i256 %1)
  call void @llvm.evm.mstore(i256 0, i256 %2)
  call void @llvm.evm.return(i256 0, i256 32)
  unreachable
}

define signext i256 @foo_cmp1(i256 %a, i256 %b) {
  %1 = icmp sgt i256 %a, %b
  br i1 %1, label %2, label %4

; <label>:2                                       ; preds = %0
  %3 = mul i256 %b, %a
  br label %6

; <label>:4                                       ; preds = %0
  %5 = shl i256 %b, 3
  br label %6

; <label>:6                                       ; preds = %4, %2
  %.0 = phi i256 [ %3, %2 ], [ %5, %4 ]
  ret i256 %.0
}

