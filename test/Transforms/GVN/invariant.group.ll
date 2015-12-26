; RUN: opt < %s -gvn -S | FileCheck %s

%struct.A = type { i32 (...)** }
@_ZTV1A = available_externally unnamed_addr constant [3 x i8*] [i8* null, i8* bitcast (i8** @_ZTI1A to i8*), i8* bitcast (void (%struct.A*)* @_ZN1A3fooEv to i8*)], align 8
@_ZTI1A = external constant i8*

@unknownPtr = external global i8

; CHECK-LABEL: define i8 @simple() {
define i8 @simple() {
entry:
    %ptr = alloca i8
    store i8 42, i8* %ptr, !invariant.group !0
    call void @foo(i8* %ptr)

    %a = load i8, i8* %ptr, !invariant.group !0
    %b = load i8, i8* %ptr, !invariant.group !0
    %c = load i8, i8* %ptr, !invariant.group !0
; CHECK: ret i8 42
    ret i8 %a
}

; CHECK-LABEL: define i8 @optimizable1() {
define i8 @optimizable1() {
entry:
    %ptr = alloca i8
    store i8 42, i8* %ptr, !invariant.group !0
    %ptr2 = call i8* @llvm.invariant.group.barrier(i8* %ptr)
    %a = load i8, i8* %ptr, !invariant.group !0
    
    call void @foo(i8* %ptr2); call to use %ptr2
; CHECK: ret i8 42
    ret i8 %a
}

; CHECK-LABEL: define i8 @optimizable2() {
define i8 @optimizable2() {
entry:
    %ptr = alloca i8
    store i8 42, i8* %ptr, !invariant.group !0
    call void @foo(i8* %ptr)
    
    store i8 13, i8* %ptr ; can't use this store with invariant.group
    %a = load i8, i8* %ptr 
    call void @bar(i8 %a) ; call to use %a
    
    call void @foo(i8* %ptr)
    %b = load i8, i8* %ptr, !invariant.group !0
    
; CHECK: ret i8 42
    ret i8 %b
}

; CHECK-LABEL: define i8 @unoptimizable1() {
define i8 @unoptimizable1() {
entry:
    %ptr = alloca i8
    store i8 42, i8* %ptr
    call void @foo(i8* %ptr)
    %a = load i8, i8* %ptr, !invariant.group !0
; CHECK: ret i8 %a
    ret i8 %a
}

; CHECK-LABEL: define void @indirectLoads() {
define void @indirectLoads() {
entry:
  %a = alloca %struct.A*, align 8
  %0 = bitcast %struct.A** %a to i8*
  
  %call = call i8* @getPointer(i8* null) 
  %1 = bitcast i8* %call to %struct.A*
  call void @_ZN1AC1Ev(%struct.A* %1)
  %2 = bitcast %struct.A* %1 to i8***
  
; CHECK: %vtable = load {{.*}} !invariant.group
  %vtable = load i8**, i8*** %2, align 8, !invariant.group !2
  %cmp.vtables = icmp eq i8** %vtable, getelementptr inbounds ([3 x i8*], [3 x i8*]* @_ZTV1A, i64 0, i64 2)
  call void @llvm.assume(i1 %cmp.vtables)
  
  store %struct.A* %1, %struct.A** %a, align 8
  %3 = load %struct.A*, %struct.A** %a, align 8
  %4 = bitcast %struct.A* %3 to void (%struct.A*)***

; CHECK: call void @_ZN1A3fooEv(
  %vtable1 = load void (%struct.A*)**, void (%struct.A*)*** %4, align 8, !invariant.group !2
  %vfn = getelementptr inbounds void (%struct.A*)*, void (%struct.A*)** %vtable1, i64 0
  %5 = load void (%struct.A*)*, void (%struct.A*)** %vfn, align 8
  call void %5(%struct.A* %3)
  %6 = load %struct.A*, %struct.A** %a, align 8
  %7 = bitcast %struct.A* %6 to void (%struct.A*)***

; CHECK: call void @_ZN1A3fooEv(
  %vtable2 = load void (%struct.A*)**, void (%struct.A*)*** %7, align 8, !invariant.group !2
  %vfn3 = getelementptr inbounds void (%struct.A*)*, void (%struct.A*)** %vtable2, i64 0
  %8 = load void (%struct.A*)*, void (%struct.A*)** %vfn3, align 8
  
  call void %8(%struct.A* %6)
  %9 = load %struct.A*, %struct.A** %a, align 8
  %10 = bitcast %struct.A* %9 to void (%struct.A*)***
  
  %vtable4 = load void (%struct.A*)**, void (%struct.A*)*** %10, align 8, !invariant.group !2
  %vfn5 = getelementptr inbounds void (%struct.A*)*, void (%struct.A*)** %vtable4, i64 0
  %11 = load void (%struct.A*)*, void (%struct.A*)** %vfn5, align 8
; CHECK: call void @_ZN1A3fooEv(
  call void %11(%struct.A* %9)
 
  %vtable5 = load i8**, i8*** %2, align 8, !invariant.group !2
  %vfn6 = getelementptr inbounds i8*, i8** %vtable5, i64 0
  %12 = bitcast i8** %vfn6 to void (%struct.A*)**
  %13 = load void (%struct.A*)*, void (%struct.A*)** %12, align 8
; CHECK: call void @_ZN1A3fooEv(
  call void %13(%struct.A* %9)
  
  ret void
}

; CHECK-LABEL: define void @combiningBitCastWithLoad() {
define void @combiningBitCastWithLoad() {
entry:
  %a = alloca %struct.A*, align 8
  %0 = bitcast %struct.A** %a to i8*
  
  %call = call i8* @getPointer(i8* null) 
  %1 = bitcast i8* %call to %struct.A*
  call void @_ZN1AC1Ev(%struct.A* %1)
  %2 = bitcast %struct.A* %1 to i8***
  
; CHECK: %vtable = load {{.*}} !invariant.group
  %vtable = load i8**, i8*** %2, align 8, !invariant.group !2
  %cmp.vtables = icmp eq i8** %vtable, getelementptr inbounds ([3 x i8*], [3 x i8*]* @_ZTV1A, i64 0, i64 2)
  
  store %struct.A* %1, %struct.A** %a, align 8
; CHECK-NOT: !invariant.group
  %3 = load %struct.A*, %struct.A** %a, align 8
  %4 = bitcast %struct.A* %3 to void (%struct.A*)***

  %vtable1 = load void (%struct.A*)**, void (%struct.A*)*** %4, align 8, !invariant.group !2
  %vfn = getelementptr inbounds void (%struct.A*)*, void (%struct.A*)** %vtable1, i64 0
  %5 = load void (%struct.A*)*, void (%struct.A*)** %vfn, align 8
  call void %5(%struct.A* %3)

  ret void
}

; CHECK-LABEL:define void @loadCombine() {
define void @loadCombine() {
enter:
  %ptr = alloca i8
  store i8 42, i8* %ptr
  call void @foo(i8* %ptr)
; CHECK: %[[A:.*]] = load i8, i8* %ptr, !invariant.group
  %a = load i8, i8* %ptr, !invariant.group !0
; CHECK-NOT: load
  %b = load i8, i8* %ptr, !invariant.group !1
; CHECK: call void @bar(i8 %[[A]])
  call void @bar(i8 %a)
; CHECK: call void @bar(i8 %[[A]])
  call void @bar(i8 %b)
  ret void
}

; CHECK-LABEL: define void @loadCombine1() {
define void @loadCombine1() {
enter:
  %ptr = alloca i8
  store i8 42, i8* %ptr
  call void @foo(i8* %ptr)
; CHECK: %[[D:.*]] = load i8, i8* %ptr, !invariant.group
  %c = load i8, i8* %ptr
; CHECK-NOT: load
  %d = load i8, i8* %ptr, !invariant.group !1
; CHECK: call void @bar(i8 %[[D]])
  call void @bar(i8 %c)
; CHECK: call void @bar(i8 %[[D]])
  call void @bar(i8 %d)
  ret void
}

; CHECK-LABEL: define void @loadCombine2() {    
define void @loadCombine2() {
enter:
  %ptr = alloca i8
  store i8 42, i8* %ptr
  call void @foo(i8* %ptr)
; CHECK: %[[E:.*]] = load i8, i8* %ptr, !invariant.group
  %e = load i8, i8* %ptr, !invariant.group !1
; CHECK-NOT: load
  %f = load i8, i8* %ptr
; CHECK: call void @bar(i8 %[[E]])
  call void @bar(i8 %e)
; CHECK: call void @bar(i8 %[[E]])
  call void @bar(i8 %f)
  ret void
}

; CHECK-LABEL: define void @loadCombine3() {
define void @loadCombine3() {
enter:
  %ptr = alloca i8
  store i8 42, i8* %ptr
  call void @foo(i8* %ptr)
; CHECK: %[[E:.*]] = load i8, i8* %ptr, !invariant.group ![[OneMD:[0-9]]]
  %e = load i8, i8* %ptr, !invariant.group !1
; CHECK-NOT: load
  %f = load i8, i8* %ptr, !invariant.group !1
; CHECK: call void @bar(i8 %[[E]])
  call void @bar(i8 %e)
; CHECK: call void @bar(i8 %[[E]])
  call void @bar(i8 %f)
  ret void
}

; CHECK-LABEL: define i8 @unoptimizable2() {
define i8 @unoptimizable2() {
entry:
    %ptr = alloca i8
    store i8 42, i8* %ptr
    call void @foo(i8* %ptr)
    %a = load i8, i8* %ptr
    call void @foo(i8* %ptr)
    %b = load i8, i8* %ptr, !invariant.group !0
    
; CHECK: ret i8 %a
    ret i8 %a
}

; CHECK-LABEL: define i8 @unoptimizable3() {
define i8 @unoptimizable3() {
entry:
    %ptr = alloca i8
    store i8 42, i8* %ptr, !invariant.group !0
    %ptr2 = call i8* @getPointer(i8* %ptr)
    %a = load i8, i8* %ptr2, !invariant.group !0
    
; CHECK: ret i8 %a
    ret i8 %a
}

; CHECK-LABEL: define i8 @unoptimizable4() {
define i8 @unoptimizable4() {
entry:
    %ptr = alloca i8
    store i8 42, i8* %ptr, !invariant.group !0
    %ptr2 = call i8* @llvm.invariant.group.barrier(i8* %ptr)
    %a = load i8, i8* %ptr2, !invariant.group !0
    
; CHECK: ret i8 %a
    ret i8 %a
}

; CHECK-LABEL: define i8 @volatile1() {
define i8 @volatile1() {
entry:
    %ptr = alloca i8
    store i8 42, i8* %ptr, !invariant.group !0
    call void @foo(i8* %ptr)
    %a = load i8, i8* %ptr, !invariant.group !0
    %b = load volatile i8, i8* %ptr
; CHECK: call void @bar(i8 %b)
    call void @bar(i8 %b)

    %c = load volatile i8, i8* %ptr, !invariant.group !0
; FIXME: we could change %c to 42, preserving volatile load
; CHECK: call void @bar(i8 %c)
    call void @bar(i8 %c)
; CHECK: ret i8 42
    ret i8 %a
}

; CHECK-LABEL: define i8 @volatile2() {
define i8 @volatile2() {
entry:
    %ptr = alloca i8
    store i8 42, i8* %ptr, !invariant.group !0
    call void @foo(i8* %ptr)
    %a = load i8, i8* %ptr, !invariant.group !0
    %b = load volatile i8, i8* %ptr
; CHECK: call void @bar(i8 %b)
    call void @bar(i8 %b)

    %c = load volatile i8, i8* %ptr, !invariant.group !0
; FIXME: we could change %c to 42, preserving volatile load
; CHECK: call void @bar(i8 %c)
    call void @bar(i8 %c)
; CHECK: ret i8 42
    ret i8 %a
}

; CHECK-LABEL: define i8 @fun() {
define i8 @fun() {
entry:
    %ptr = alloca i8
    store i8 42, i8* %ptr, !invariant.group !0
    call void @foo(i8* %ptr)

    %a = load i8, i8* %ptr, !invariant.group !0 ; Can assume that value under %ptr didn't change
; CHECK: call void @bar(i8 42)
    call void @bar(i8 %a)
    
    call void @foo(i8* %ptr)
    %b = load i8, i8* %ptr, !invariant.group !1 ; Can't assume anything, because group changed
; CHECK: call void @bar(i8 %b)
    call void @bar(i8 %b)
    
    %newPtr = call i8* @getPointer(i8* %ptr) 
    %c = load i8, i8* %newPtr, !invariant.group !0 ; Can't assume anything, because we only have information about %ptr
; CHECK: call void @bar(i8 %c)
    call void @bar(i8 %c)
    
    %unknownValue = load i8, i8* @unknownPtr
; FIXME: Can assume that %unknownValue == 42
; CHECK: store i8 %unknownValue, i8* %ptr, !invariant.group !0
    store i8 %unknownValue, i8* %ptr, !invariant.group !0 

    %newPtr2 = call i8* @llvm.invariant.group.barrier(i8* %ptr)
    %d = load i8, i8* %newPtr2, !invariant.group !0  ; Can't step through invariant.group.barrier to get value of %ptr
; CHECK: ret i8 %d
    ret i8 %d
}

declare void @foo(i8*)
declare void @bar(i8)
declare i8* @getPointer(i8*)
declare void @_ZN1A3fooEv(%struct.A*)
declare void @_ZN1AC1Ev(%struct.A*)
declare i8* @llvm.invariant.group.barrier(i8*)

; Function Attrs: nounwind
declare void @llvm.assume(i1 %cmp.vtables) #0


attributes #0 = { nounwind }
; CHECK: ![[OneMD]] = !{!"other ptr"}
!0 = !{!"magic ptr"}
!1 = !{!"other ptr"}
!2 = !{!"vtable_of_a"}
