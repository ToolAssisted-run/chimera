; ModuleID = 'probe6.5e8a2b5370214e02-cgu.0'
source_filename = "probe6.5e8a2b5370214e02-cgu.0"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; probe6::probe
; Function Attrs: nonlazybind uwtable
define void @_RNvCs87eqC9UjApq_6probe65probe() unnamed_addr #0 {
start:
; call <f64>::is_subnormal
  %_1 = call zeroext i1 @_RNvMNtCs4NRVxsYgnAr_4core3f64d12is_subnormalCs87eqC9UjApq_6probe6(double 1.000000e+00)
  ret void
}

; <f64>::is_subnormal
; Function Attrs: inlinehint nonlazybind uwtable
define internal zeroext i1 @_RNvMNtCs4NRVxsYgnAr_4core3f64d12is_subnormalCs87eqC9UjApq_6probe6(double %self) unnamed_addr #1 {
start:
  %_2 = alloca [1 x i8], align 1
  %b = bitcast double %self to i64
  %_5 = and i64 %b, 4503599627370495
  %_6 = and i64 %b, 9218868437227405312
  %0 = icmp eq i64 %_5, 0
  br i1 %0, label %bb1, label %bb8

bb1:                                              ; preds = %start
  %1 = icmp eq i64 %_6, 9218868437227405312
  br i1 %1, label %bb6, label %bb9

bb8:                                              ; preds = %start
  switch i64 %_6, label %bb2 [
    i64 9218868437227405312, label %bb5
    i64 0, label %bb3
  ]

bb6:                                              ; preds = %bb1
  store i8 1, ptr %_2, align 1
  br label %bb7

bb9:                                              ; preds = %bb1
  switch i64 %_6, label %bb2 [
    i64 9218868437227405312, label %bb5
    i64 0, label %bb4
  ]

bb7:                                              ; preds = %bb2, %bb3, %bb5, %bb4, %bb6
  %2 = load i8, ptr %_2, align 1
  %_3 = zext i8 %2 to i64
  %_0 = icmp eq i64 %_3, 3
  ret i1 %_0

bb2:                                              ; preds = %bb8, %bb9
  store i8 4, ptr %_2, align 1
  br label %bb7

bb5:                                              ; preds = %bb8, %bb9
  store i8 0, ptr %_2, align 1
  br label %bb7

bb4:                                              ; preds = %bb9
  store i8 2, ptr %_2, align 1
  br label %bb7

bb3:                                              ; preds = %bb8
  store i8 3, ptr %_2, align 1
  br label %bb7
}

attributes #0 = { nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #1 = { inlinehint nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}

!0 = !{i32 8, !"PIC Level", i32 2}
!1 = !{i32 2, !"RtLibUseGOT", i32 1}
!2 = !{!"rustc version 1.97.1 (8bab26f4f 2026-07-14)"}
