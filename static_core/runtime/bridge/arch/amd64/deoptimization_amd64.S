/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "arch/asm_support.h"

.extern InvokeInterpreter

.global DeoptimizeAfterCFrame
.type DeoptimizeAfterCFrame, %function
DeoptimizeAfterCFrame:
    CFI_STARTPROC

    // Parameters:
    // %rdi - thread
    // %rsi - pc of the entry
    // %rdx - pointer to interpreter Frame
    // %rcx - pointer to cframe origin
    // %r8  - last restored interpreter Frame
    // %r9  - callee reg_mask
    // 8(%rsp) - callee vreg_mask

    // Morph CFrame into C2I boundary frame
    // The CFrame layout already matches the new boundary frame layout:
    //   [fp+8]  = lr           (CFrame LrSlot, RETURN_OFFSET=1 + standard saved_lr)
    //   [fp+0]  = saved_fp     (CFrame PrevFrameSlot, FP_OFFSET=0 + standard saved_fp)
    //   [fp-8]  = marker       (CFrame MethodSlot, overwritten with COMPILED_CODE_TO_INTERPRETER)
    //   [fp-16] = flags        (CFrame FlagsSlot)
    //   [fp-...] = callee regs (CFrame callee regs area, CALLEES_OFFSET=-2)
    // So we only need to overwrite the MethodSlot with the marker value.
    CFI_DEF_CFA(rcx, (2 * 8))
    CFI_REL_OFFSET(rbp, 0)

    leaq -(((CFRAME_HEADER_SIZE - 2) * 8) + CALLEE_SAVED_SIZE)(%rcx), %rsp

    movq %rcx, %rbp
    CFI_DEF_CFA_REGISTER(rbp)

    movq $COMPILED_CODE_TO_INTERPRETER_BRIDGE, (INT_METHOD_OFFSET * 8)(%rcx)

    // Set IFrame's prev_frame to this C2I bridge frame
    movq %rcx, FRAME_PREV_FRAME_OFFSET(%r8)

    BOUNDARY_FRAME_SLOT = ((CFRAME_HEADER_SIZE - 3) + 2)

    // Copy callee saved registers from cframe into boundary frame.
    leaq -(CFRAME_CALLEE_REGS_START_SLOT * 8)(%rcx), %r12
    leaq -((BOUNDARY_FRAME_SLOT) * 8)(%rcx), %r13

    movq (%r12), %rbx
    movq %rbx, (%r13)
    CFI_REL_OFFSET(THREAD_REG, -((BOUNDARY_FRAME_SLOT + 0) * 8))
    movq -8(%r12), %rbx
    movq %rbx, -8(%r13)
    CFI_REL_OFFSET(r14, -((BOUNDARY_FRAME_SLOT + 1) * 8))
    movq -16(%r12), %rbx
    movq %rbx, -16(%r13)
    CFI_REL_OFFSET(r13, -((BOUNDARY_FRAME_SLOT + 2) * 8))
    movq -24(%r12), %rbx
    movq %rbx, -24(%r13)
    CFI_REL_OFFSET(r12, -((BOUNDARY_FRAME_SLOT + 3) * 8))
    movq -32(%r12), %rbx
    movq %rbx, -32(%r13)
    CFI_REL_OFFSET(rbx, -((BOUNDARY_FRAME_SLOT + 4) * 8))

    // Save used registers
    pushq %r8
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi

    movq %r8, %rcx
    // Arguments are already lie in the registers, because signature of DeoptimizeAfterCFrame is similar to InvokeInterpreter
    callq InvokeInterpreter@plt
    movq %rax, %xmm0

    // Restore used registers
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %r8

    // Restore stack pointer
    movq %rcx, %rsp

    // Restore callee saved registers
    subq $(BOUNDARY_FRAME_SLOT * 8), %rcx
    movq (%rcx), %THREAD_REG
    movq -8(%rcx), %r14
    CFI_RESTORE(r14)
    movq -16(%rcx), %r13
    CFI_RESTORE(r13)
    movq -24(%rcx), %r12
    CFI_RESTORE(r12)
    movq -32(%rcx), %rbx
    CFI_RESTORE(rbx)

    // Restore thread register
    movq %rdi, %THREAD_REG
    CFI_RESTORE(THREAD_REG)

    // Restore standard FP frame: [rbp+0]=saved_fp, [rbp+8]=lr
    movq %rbp, %rsp
    popq %rbp
    CFI_RESTORE(rbp)
    CFI_DEF_CFA(rsp, 8)
    retq
    CFI_ENDPROC

.global DeoptimizeAfterIFrame
.type DeoptimizeAfterIFrame, %function
DeoptimizeAfterIFrame:
    CFI_STARTPROC

    // Parameters:
    // %rdi - thread
    // %rsi - pc of the entry
    // %rdx - pointer to interpreter Frame
    // %rcx - pointer to cframe origin
    // %r8  - last restored interpreter Frame

    CFI_DEF_CFA(rcx, (2 * 8))
    CFI_REL_OFFSET(rbp, 0)
    CFI_REL_OFFSET(r15, -((CFRAME_CALLEE_REGS_START_SLOT + 0) * 8))
    CFI_REL_OFFSET(r14, -((CFRAME_CALLEE_REGS_START_SLOT + 1) * 8))
    CFI_REL_OFFSET(r13, -((CFRAME_CALLEE_REGS_START_SLOT + 2) * 8))
    CFI_REL_OFFSET(r12, -((CFRAME_CALLEE_REGS_START_SLOT + 3) * 8))
    CFI_REL_OFFSET(rbx, -((CFRAME_CALLEE_REGS_START_SLOT + 4) * 8))

    movq %rcx, %rbp
    CFI_DEF_CFA_REGISTER(rbp)

    // Restore stack pointer to the fp pointer of the cframe
    movq %rcx, %rsp

    // Restore callee saved registers
    subq $(CFRAME_CALLEE_REGS_START_SLOT * 8), %rcx
    movq (%rcx), %r15
    CFI_RESTORE(r15)
    movq -8(%rcx), %r14
    CFI_RESTORE(r14)
    movq -16(%rcx), %r13
    CFI_RESTORE(r13)
    movq -24(%rcx), %r12
    CFI_RESTORE(r12)
    movq -32(%rcx), %rbx
    CFI_RESTORE(rbx)

    movq %r8, %rcx
    callq InvokeInterpreter@plt
    // InvokeInterpreter returns int64 value, but result can be double, so we copy value to vector register
    movq %rax, %xmm0

    popq %rbp
    CFI_RESTORE(rbp)
    CFI_DEF_CFA(rsp, (1 * 8))
    retq
    CFI_ENDPROC

.global DropCompiledFrameAndReturn
.type DropCompiledFrameAndReturn, %function
DropCompiledFrameAndReturn:
    CFI_STARTPROC

    // %rdi - pointer to cframe origin

    CFI_DEF_CFA(rdi, (2 * 8))
    CFI_REL_OFFSET(rbp, 0)
    CFI_REL_OFFSET(r15, -((CFRAME_CALLEE_REGS_START_SLOT + 0) * 8))
    CFI_REL_OFFSET(r14, -((CFRAME_CALLEE_REGS_START_SLOT + 1) * 8))
    CFI_REL_OFFSET(r13, -((CFRAME_CALLEE_REGS_START_SLOT + 2) * 8))
    CFI_REL_OFFSET(r12, -((CFRAME_CALLEE_REGS_START_SLOT + 3) * 8))
    CFI_REL_OFFSET(rbx, -((CFRAME_CALLEE_REGS_START_SLOT + 4) * 8))

    movq %rdi, %rbp
    CFI_DEF_CFA_REGISTER(rbp)
    movq %rdi, %rsp

    // Restore callee saved registers from dropped CFrame
    subq $(CFRAME_CALLEE_REGS_START_SLOT * 8), %rdi
    movq (%rdi), %r15
    CFI_RESTORE(r15)
    movq -8(%rdi), %r14
    CFI_RESTORE(r14)
    movq -16(%rdi), %r13
    CFI_RESTORE(r13)
    movq -24(%rdi), %r12
    CFI_RESTORE(r12)
    movq -32(%rdi), %rbx
    CFI_RESTORE(rbx)

    // We need to clear return value, since it will be written to the IFrame's accumulator. Without this, it holds
    // garbage and StackWalker verification might fail.
    xorq %rax, %rax

    popq %rbp
    CFI_RESTORE(rbp)
    CFI_DEF_CFA(rsp, (1 * 8))
    retq
    CFI_ENDPROC
