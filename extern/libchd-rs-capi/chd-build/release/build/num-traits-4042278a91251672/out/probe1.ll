; ModuleID = 'probe1.41250c9c5896e374-cgu.0'
source_filename = "probe1.41250c9c5896e374-cgu.0"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; <f64>::to_int_unchecked::<i32>
; Function Attrs: inlinehint nonlazybind uwtable
define i32 @_RINvMNtCs4NRVxsYgnAr_4core3f64d16to_int_uncheckedlECs5ALmbnXwYwO_6probe1(double %self) unnamed_addr #0 {
start:
; call <f64 as core::convert::num::FloatToInt<i32>>::to_int_unchecked
  %_0 = call i32 @_RNvXsx_NtNtCs4NRVxsYgnAr_4core7convert3numdINtB5_10FloatToIntlE16to_int_uncheckedCs5ALmbnXwYwO_6probe1(double %self)
  ret i32 %_0
}

; probe1::probe
; Function Attrs: nonlazybind uwtable
define void @_RNvCs5ALmbnXwYwO_6probe15probe() unnamed_addr #1 {
start:
; call <f64>::to_int_unchecked::<i32>
  %_1 = call i32 @_RINvMNtCs4NRVxsYgnAr_4core3f64d16to_int_uncheckedlECs5ALmbnXwYwO_6probe1(double 1.000000e+00)
  ret void
}

; <f64 as core::convert::num::FloatToInt<i32>>::to_int_unchecked
; Function Attrs: inlinehint nonlazybind uwtable
define internal i32 @_RNvXsx_NtNtCs4NRVxsYgnAr_4core7convert3numdINtB5_10FloatToIntlE16to_int_uncheckedCs5ALmbnXwYwO_6probe1(double %self) unnamed_addr #0 {
start:
  %0 = alloca [4 x i8], align 4
  %1 = fptosi double %self to i32
  store i32 %1, ptr %0, align 4
  %_0 = load i32, ptr %0, align 4
  ret i32 %_0
}

attributes #0 = { inlinehint nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #1 = { nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}

!0 = !{i32 8, !"PIC Level", i32 2}
!1 = !{i32 2, !"RtLibUseGOT", i32 1}
!2 = !{!"rustc version 1.97.1 (8bab26f4f 2026-07-14)"}
