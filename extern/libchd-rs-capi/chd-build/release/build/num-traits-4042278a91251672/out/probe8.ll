; ModuleID = 'probe8.8fac890571442611-cgu.0'
source_filename = "probe8.8fac890571442611-cgu.0"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; probe8::probe
; Function Attrs: nonlazybind uwtable
define void @_RNvCsckLVYWqMPVl_6probe85probe() unnamed_addr #0 {
start:
  %0 = alloca [4 x i8], align 4
  %_1 = alloca [4 x i8], align 1
; call <u32>::to_ne_bytes
  %1 = call i32 @_RNvMs6_NtCs4NRVxsYgnAr_4core3numm11to_ne_bytesCsckLVYWqMPVl_6probe8(i32 1)
  store i32 %1, ptr %0, align 4
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %_1, ptr align 4 %0, i64 4, i1 false)
  ret void
}

; <u32>::to_ne_bytes
; Function Attrs: inlinehint nonlazybind uwtable
define internal i32 @_RNvMs6_NtCs4NRVxsYgnAr_4core3numm11to_ne_bytesCsckLVYWqMPVl_6probe8(i32 %self) unnamed_addr #1 {
start:
  %_0 = alloca [4 x i8], align 1
  store i32 %self, ptr %_0, align 1
  %0 = load i32, ptr %_0, align 1
  ret i32 %0
}

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i64(ptr noalias writeonly captures(none), ptr noalias readonly captures(none), i64, i1 immarg) #2

attributes #0 = { nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #1 = { inlinehint nonlazybind uwtable "probe-stack"="inline-asm" "target-cpu"="x86-64" }
attributes #2 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}

!0 = !{i32 8, !"PIC Level", i32 2}
!1 = !{i32 2, !"RtLibUseGOT", i32 1}
!2 = !{!"rustc version 1.97.1 (8bab26f4f 2026-07-14)"}
