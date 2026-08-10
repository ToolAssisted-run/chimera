; ModuleID = 'probe5.bf4acc597fd4882d-cgu.0'
source_filename = "probe5.bf4acc597fd4882d-cgu.0"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; probe5::probe
; Function Attrs: nonlazybind uwtable
define void @_RNvCsgqf77Hl6S9B_6probe55probe() unnamed_addr #0 {
start:
; call <f64>::copysign
  %_1 = call double @_RNvMNtCs4NRVxsYgnAr_4core3f64d8copysignCsgqf77Hl6S9B_6probe5(double 1.000000e+00, double -1.000000e+00)
  ret void
}

; <f64>::copysign
; Function Attrs: inlinehint nonlazybind uwtable
define internal double @_RNvMNtCs4NRVxsYgnAr_4core3f64d8copysignCsgqf77Hl6S9B_6probe5(double %self, double %sign) unnamed_addr #1 {
start:
  %0 = alloca [8 x i8], align 8
  %1 = call double @llvm.copysign.f64(double %self, double %sign)
  store double %1, ptr %0, align 8
  %_0 = load double, ptr %0, align 8
  ret double %_0
}

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare double @llvm.copysign.f64(double, double) #2

attributes #0 = { nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #1 = { inlinehint nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #2 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}

!0 = !{i32 8, !"PIC Level", i32 2}
!1 = !{i32 2, !"RtLibUseGOT", i32 1}
!2 = !{!"rustc version 1.97.1 (8bab26f4f 2026-07-14)"}
