/*
 * Copyright (c) 2021-2023 Huawei Device Co., Ltd.
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
/*
Low-level calling convention
*/
#include "target/aarch64/target.h"

namespace ark::compiler::aarch64 {

constexpr int32_t IMM_2 = 2;

Aarch64CallingConvention::Aarch64CallingConvention(ArenaAllocator *allocator, Encoder *enc, RegistersDescription *descr,
                                                   CallConvMode mode)
    : CallingConvention(allocator, enc, descr, mode)
{
}

ParameterInfo *Aarch64CallingConvention::GetParameterInfo(uint8_t regsOffset)
{
    auto paramInfo = GetAllocator()->New<aarch64::Aarch64ParameterInfo>();
    for (int i = 0; i < regsOffset; ++i) {
        paramInfo->GetNativeParam(INT64_TYPE);
    }
    return paramInfo;
}

void *Aarch64CallingConvention::GetCodeEntry()
{
    return reinterpret_cast<void *>(GetMasm()->GetInstructionAt(0));
}

uint32_t Aarch64CallingConvention::GetCodeSize()
{
    return GetMasm()->GetSizeOfCodeGenerated();
}

size_t Aarch64CallingConvention::PushRegs(vixl::aarch64::CPURegList regs, vixl::aarch64::CPURegList vregs,
                                          bool isCallee)
{
    if ((regs.GetCount() % IMM_2) == 1) {
        ASSERT((regs.GetList() & (UINT64_C(1) << vixl::aarch64::xzr.GetCode())) == 0);
        regs.Combine(vixl::aarch64::xzr);
    }
    if ((vregs.GetCount() % IMM_2) == 1) {
        auto regdescr = static_cast<Aarch64RegisterDescription *>(GetRegfile());
        uint8_t allignmentVreg = regdescr->GetAlignmentVreg(isCallee);
        ASSERT((vregs.GetList() & (UINT64_C(1) << allignmentVreg)) == 0);
        vregs.Combine(allignmentVreg);
    }
    GetMasm()->PushCPURegList(vregs);
    GetMasm()->PushCPURegList(regs);
    return vregs.GetCount() + regs.GetCount();
}

size_t Aarch64CallingConvention::PopRegs(vixl::aarch64::CPURegList regs, vixl::aarch64::CPURegList vregs, bool isCallee)
{
    if ((regs.GetCount() % IMM_2) == 1) {
        ASSERT((regs.GetList() & (UINT64_C(1) << vixl::aarch64::xzr.GetCode())) == 0);
        regs.Combine(vixl::aarch64::xzr);
    }
    if ((vregs.GetCount() % IMM_2) == 1) {
        auto regdescr = static_cast<Aarch64RegisterDescription *>(GetRegfile());
        uint8_t allignmentVreg = regdescr->GetAlignmentVreg(isCallee);
        ASSERT((vregs.GetList() & (UINT64_C(1) << allignmentVreg)) == 0);
        vregs.Combine(allignmentVreg);
    }
    GetMasm()->PopCPURegList(regs);
    GetMasm()->PopCPURegList(vregs);
    return vregs.GetCount() + regs.GetCount();
}

std::variant<Reg, uint8_t> Aarch64ParameterInfo::GetNativeParam(const TypeInfo &type)
{
    if (type.IsFloat()) {
        if (currentVectorNumber_ > MAX_VECTOR_PARAM_ID) {
            return currentStackOffset_++;
        }
        return Reg(currentVectorNumber_++, type);
    }
    if (currentScalarNumber_ > MAX_SCALAR_PARAM_ID) {
        return currentStackOffset_++;
    }
    auto ret = Reg(currentScalarNumber_++, type);
    if (type.GetSize() > DOUBLE_WORD_SIZE) {
        currentScalarNumber_++;
    }
    return ret;
}

Location Aarch64ParameterInfo::GetNextLocation(DataType::Type type)
{
    if (DataType::IsFloatType(type)) {
        if (currentVectorNumber_ > MAX_VECTOR_PARAM_ID) {
            return Location::MakeStackArgument(currentStackOffset_++);
        }
        return Location::MakeFpRegister(currentVectorNumber_++);
    }
    if (currentScalarNumber_ > MAX_SCALAR_PARAM_ID) {
        return Location::MakeStackArgument(currentStackOffset_++);
    }
    Target target(Arch::AARCH64);
    return Location::MakeRegister(target.GetParamRegId(currentScalarNumber_++));
}

Reg Aarch64CallingConvention::InitFlagsReg(bool hasFloatRegs)
{
    auto flags {static_cast<uint64_t>(hasFloatRegs) << CFrameLayout::HasFloatRegsFlag::START_BIT};
    auto flagsReg {GetTarget().GetZeroReg()};
    if (flags != 0U) {
        flagsReg = GetTarget().GetLinkReg();
        GetEncoder()->EncodeMov(flagsReg, Imm(flags));
    }
    return flagsReg;
}

using vixl::aarch64::CPURegList, vixl::aarch64::CPURegister, vixl::aarch64::MemOperand;

void Aarch64CallingConvention::GeneratePrologue(const FrameInfo &frameInfo)
{
    static_assert((CFrameLayout::GetLocalsCount() & 1U) == 0);
    auto encoder = GetEncoder();
    const CFrameLayout &fl = encoder->GetFrameLayout();
    auto regdescr = static_cast<Aarch64RegisterDescription *>(GetRegfile());
    auto sp = GetTarget().GetStackReg();
    auto fp = GetTarget().GetFrameReg();
    auto lr = GetTarget().GetLinkReg();
    auto spToRegsSlots = CFrameLayout::GetTopToRegsSlotsCount();

    // Save FP and LR
    if (frameInfo.GetSaveFrameAndLinkRegs() || ProvideCFI()) {
        static_assert(CFrameLayout::GetTopToRegsSlotsCount() > CFrameLayout::GetFpLrSlotsCount());
        GetMasm()->PushCPURegList(vixl::aarch64::CPURegList(VixlReg(fp), VixlReg(lr)));
        SET_CFI_OFFSET(pushFplr, encoder->GetCursorOffset());
        spToRegsSlots -= CFrameLayout::GetFpLrSlotsCount();
    }

    // Setup FP
    if (frameInfo.GetSetupFrame() || ProvideCFI()) {
        // If SetupFrame flag is set, then SaveFrameAndLinkRegs must be set also.
        // These are separate flags as it looks like Irtoc does not need frame setup
        // but requires to save frame and link regs.
        ASSERT(!frameInfo.GetSetupFrame() || frameInfo.GetSaveFrameAndLinkRegs());
        encoder->EncodeMov(fp, sp);
        SET_CFI_OFFSET(setFp, encoder->GetCursorOffset());
    }

    if (IsDynCallMode() && GetDynInfo().IsCheckRequired()) {
        static_assert(CallConvDynInfo::REG_NUM_ARGS == 1);
        static_assert(CallConvDynInfo::REG_COUNT == CallConvDynInfo::REG_NUM_ARGS + 1);

        ASSERT(frameInfo.GetSaveFrameAndLinkRegs());

        constexpr auto NUM_ACTUAL_REG = GetTarget().GetParamReg(CallConvDynInfo::REG_NUM_ARGS);
        constexpr auto NUM_EXPECTED_REG = GetTarget().GetParamReg(CallConvDynInfo::REG_COUNT);
        auto numExpected = GetDynInfo().GetNumExpectedArgs();

        auto expandDone = encoder->CreateLabel();
        encoder->EncodeJump(expandDone, NUM_ACTUAL_REG, Imm(numExpected), Condition::GE);
        encoder->EncodeMov(NUM_EXPECTED_REG, Imm(numExpected));

        MemRef expandEntrypoint(Reg(GetThreadReg(Arch::AARCH64), GetTarget().GetPtrRegType()),
                                GetDynInfo().GetExpandEntrypointTlsOffset());
        GetEncoder()->MakeCall(expandEntrypoint);
        encoder->BindLabel(expandDone);
    }

    // Reset flags and setup method
    if (frameInfo.GetSetupFrame()) {
        static_assert(CFrameMethod::End() == CFrameFlags::Start());
        constexpr int64_t SLOTS_COUNT = CFrameMethod::GetSize() + CFrameFlags::GetSize();

        GetMasm()->Stp(VixlReg(InitFlagsReg(frameInfo.GetHasFloatRegs())),  // Reset OSR flag and set HasFloatRegsFlag
                       VixlReg(GetTarget().GetParamReg(0)),                 // Set Method pointer
                       vixl::aarch64::MemOperand(VixlReg(sp), VixlImm(-SLOTS_COUNT * fl.GetSlotSize()),
                                                 vixl::aarch64::AddrMode::PreIndex));
        spToRegsSlots -= SLOTS_COUNT;
    }

    RegMask calleeRegsMask;
    VRegMask calleeVregsMask;
    regdescr->FillUsedCalleeSavedRegisters(&calleeRegsMask, &calleeVregsMask, frameInfo.GetSaveUnusedCalleeRegs());
    SET_CFI_CALLEE_REGS(calleeRegsMask);
    SET_CFI_CALLEE_VREGS(calleeVregsMask);
    auto lastCalleeReg = spToRegsSlots + calleeRegsMask.Count();
    auto lastCalleeVreg = spToRegsSlots + fl.GetCalleeRegistersCount(false) + calleeVregsMask.Count();
    auto calleeRegs = CPURegList(CPURegister::kRegister, vixl::aarch64::kXRegSize, calleeRegsMask.GetValue());
    auto calleeVregs = CPURegList(CPURegister::kVRegister, vixl::aarch64::kXRegSize, calleeVregsMask.GetValue());
    GetMasm()->StoreCPURegList(calleeRegs, MemOperand(VixlReg(sp), VixlImm(-lastCalleeReg * fl.GetSlotSize())));
    GetMasm()->StoreCPURegList(calleeVregs, MemOperand(VixlReg(sp), VixlImm(-lastCalleeVreg * fl.GetSlotSize())));
    SET_CFI_OFFSET(pushCallees, encoder->GetCursorOffset());

    // Adjust SP
    if (frameInfo.GetAdjustSpReg()) {
        auto spToFrameEndOffset = (spToRegsSlots + fl.GetRegsSlotsCount()) * fl.GetSlotSize();
        encoder->EncodeSub(sp, sp, Imm(spToFrameEndOffset));
    }
}

void Aarch64CallingConvention::GenerateEpilogue(const FrameInfo &frameInfo, std::function<void()> postJob)
{
    auto encoder = GetEncoder();
    const CFrameLayout &fl = encoder->GetFrameLayout();
    auto regdescr = static_cast<Aarch64RegisterDescription *>(GetRegfile());
    auto sp = GetTarget().GetStackReg();
    auto fp = GetTarget().GetFrameReg();
    auto lr = GetTarget().GetLinkReg();

    if (postJob) {
        postJob();
    }

    // Restore callee-registers
    RegMask calleeRegsMask;
    VRegMask calleeVregsMask;
    regdescr->FillUsedCalleeSavedRegisters(&calleeRegsMask, &calleeVregsMask, frameInfo.GetSaveUnusedCalleeRegs());

    auto calleeRegs = CPURegList(CPURegister::kRegister, vixl::aarch64::kXRegSize, calleeRegsMask.GetValue());
    auto calleeVregs = CPURegList(CPURegister::kVRegister, vixl::aarch64::kXRegSize, calleeVregsMask.GetValue());

    if (frameInfo.GetAdjustSpReg()) {
        // SP points to the frame's bottom
        auto lastCalleeReg = fl.GetRegsSlotsCount() - calleeRegsMask.Count();
        auto lastCalleeVreg = fl.GetRegsSlotsCount() - fl.GetCalleeRegistersCount(false) - calleeVregsMask.Count();
        GetMasm()->LoadCPURegList(calleeRegs, MemOperand(VixlReg(sp), VixlImm(lastCalleeReg * fl.GetSlotSize())));
        GetMasm()->LoadCPURegList(calleeVregs, MemOperand(VixlReg(sp), VixlImm(lastCalleeVreg * fl.GetSlotSize())));
    } else {
        // SP either points to the frame's top or frame's top + FPLR slot
        auto spToRegsSlots = CFrameLayout::GetTopToRegsSlotsCount();
        if (frameInfo.GetSaveFrameAndLinkRegs() || ProvideCFI()) {
            // Adjust for FPLR slot
            spToRegsSlots -= CFrameLayout::GetFpLrSlotsCount();
        }
        auto lastCalleeReg = spToRegsSlots + calleeRegsMask.Count();
        auto lastCalleeVreg = spToRegsSlots + fl.GetCalleeRegistersCount(false) + calleeVregsMask.Count();
        GetMasm()->LoadCPURegList(calleeRegs, MemOperand(VixlReg(sp), VixlImm(-lastCalleeReg * fl.GetSlotSize())));
        GetMasm()->LoadCPURegList(calleeVregs, MemOperand(VixlReg(sp), VixlImm(-lastCalleeVreg * fl.GetSlotSize())));
    }
    SET_CFI_OFFSET(popCallees, encoder->GetCursorOffset());

    // Adjust SP
    if (frameInfo.GetAdjustSpReg()) {
        // SP points to the frame's bottom
        auto spToFrameTopSlots = fl.GetRegsSlotsCount() + CFrameRegs::Start() - CFrameReturnAddr::Start();
        if (frameInfo.GetSaveFrameAndLinkRegs() || ProvideCFI()) {
            spToFrameTopSlots -= CFrameLayout::GetFpLrSlotsCount();
        }
        auto spToFrameTopOffset = spToFrameTopSlots * fl.GetSlotSize();
        encoder->EncodeAdd(sp, sp, Imm(spToFrameTopOffset));
    }

    // Restore FP and LR
    if (IsOsrMode()) {
        encoder->EncodeAdd(sp, sp, Imm(CFrameLayout::GetFpLrSlotsCount() * fl.GetSlotSize()));
        encoder->EncodeLdp(fp, lr, false, MemRef(fp, -fl.GetOsrFpLrOffset()));
    } else if (frameInfo.GetSaveFrameAndLinkRegs() || ProvideCFI()) {
        GetMasm()->PopCPURegList(vixl::aarch64::CPURegList(VixlReg(fp), VixlReg(lr)));
    }
    SET_CFI_OFFSET(popFplr, encoder->GetCursorOffset());

    GetMasm()->Ret();
}

void Aarch64CallingConvention::GenerateNativePrologue(const FrameInfo &frameInfo)
{
    static_assert((CFrameLayout::GetLocalsCount() & 1U) == 0);
    auto encoder = GetEncoder();
    const CFrameLayout &fl = encoder->GetFrameLayout();
    auto regdescr = static_cast<Aarch64RegisterDescription *>(GetRegfile());
    auto sp = GetTarget().GetStackReg();
    auto fp = GetTarget().GetFrameReg();
    auto lr = GetTarget().GetLinkReg();
    auto spToRegsSlots = CFrameLayout::GetTopToRegsSlotsCount();

    // Save FP and LR
    if (frameInfo.GetSaveFrameAndLinkRegs() || ProvideCFI()) {
        static_assert(CFrameLayout::GetTopToRegsSlotsCount() > CFrameLayout::GetFpLrSlotsCount());
        GetMasm()->PushCPURegList(vixl::aarch64::CPURegList(VixlReg(fp), VixlReg(lr)));
        SET_CFI_OFFSET(pushFplr, encoder->GetCursorOffset());
        spToRegsSlots -= CFrameLayout::GetFpLrSlotsCount();
    }

    // 'Native' calling convention requires setting up FP for FastPath calls from IRtoC Interpreter entrypoint
    if (frameInfo.GetSetupFrame() || ProvideCFI()) {
        encoder->EncodeMov(fp, sp);
        SET_CFI_OFFSET(setFp, encoder->GetCursorOffset());
    }

    if (IsDynCallMode() && GetDynInfo().IsCheckRequired()) {
        static_assert(CallConvDynInfo::REG_NUM_ARGS == 1);
        static_assert(CallConvDynInfo::REG_COUNT == CallConvDynInfo::REG_NUM_ARGS + 1);

        ASSERT(frameInfo.GetSaveFrameAndLinkRegs());

        constexpr auto NUM_ACTUAL_REG = GetTarget().GetParamReg(CallConvDynInfo::REG_NUM_ARGS);
        constexpr auto NUM_EXPECTED_REG = GetTarget().GetParamReg(CallConvDynInfo::REG_COUNT);
        auto numExpected = GetDynInfo().GetNumExpectedArgs();

        auto expandDone = encoder->CreateLabel();
        encoder->EncodeJump(expandDone, NUM_ACTUAL_REG, Imm(numExpected), Condition::GE);
        encoder->EncodeMov(NUM_EXPECTED_REG, Imm(numExpected));

        MemRef expandEntrypoint(Reg(GetThreadReg(Arch::AARCH64), GetTarget().GetPtrRegType()),
                                GetDynInfo().GetExpandEntrypointTlsOffset());
        GetEncoder()->MakeCall(expandEntrypoint);
        encoder->BindLabel(expandDone);
    }

    // Save callee-saved registers
    RegMask calleeRegsMask;
    VRegMask calleeVregsMask;
    regdescr->FillUsedCalleeSavedRegisters(&calleeRegsMask, &calleeVregsMask, frameInfo.GetSaveUnusedCalleeRegs(),
                                           GetMode().IsOptIrtoc());
    SET_CFI_CALLEE_REGS(calleeRegsMask);
    SET_CFI_CALLEE_VREGS(calleeVregsMask);
    auto lastCalleeReg = spToRegsSlots + calleeRegsMask.Count();
    auto lastCalleeVreg = spToRegsSlots + fl.GetCalleeRegistersCount(false) + calleeVregsMask.Count();
    auto calleeRegs = CPURegList(CPURegister::kRegister, vixl::aarch64::kXRegSize, calleeRegsMask.GetValue());
    auto calleeVregs = CPURegList(CPURegister::kVRegister, vixl::aarch64::kXRegSize, calleeVregsMask.GetValue());
    GetMasm()->StoreCPURegList(calleeRegs, MemOperand(VixlReg(sp), VixlImm(-lastCalleeReg * fl.GetSlotSize())));
    GetMasm()->StoreCPURegList(calleeVregs, MemOperand(VixlReg(sp), VixlImm(-lastCalleeVreg * fl.GetSlotSize())));
    SET_CFI_OFFSET(pushCallees, encoder->GetCursorOffset());

    // Adjust SP
    if (frameInfo.GetAdjustSpReg()) {
        auto spToFrameEndOffset = (spToRegsSlots + fl.GetRegsSlotsCount()) * fl.GetSlotSize();
        encoder->EncodeSub(sp, sp, Imm(spToFrameEndOffset));
    }
}

void Aarch64CallingConvention::GenerateNativeEpilogue(const FrameInfo &frameInfo, std::function<void()> postJob)
{
    auto encoder = GetEncoder();
    const CFrameLayout &fl = encoder->GetFrameLayout();
    auto regdescr = static_cast<Aarch64RegisterDescription *>(GetRegfile());
    auto sp = GetTarget().GetStackReg();
    auto fp = GetTarget().GetFrameReg();
    auto lr = GetTarget().GetLinkReg();

    if (postJob) {
        postJob();
    }

    // Restore callee-registers
    RegMask calleeRegsMask;
    VRegMask calleeVregsMask;
    regdescr->FillUsedCalleeSavedRegisters(&calleeRegsMask, &calleeVregsMask, frameInfo.GetSaveUnusedCalleeRegs(),
                                           GetMode().IsOptIrtoc());

    auto calleeRegs = CPURegList(CPURegister::kRegister, vixl::aarch64::kXRegSize, calleeRegsMask.GetValue());
    auto calleeVregs = CPURegList(CPURegister::kVRegister, vixl::aarch64::kXRegSize, calleeVregsMask.GetValue());

    if (frameInfo.GetAdjustSpReg()) {
        // SP points to the frame's bottom
        auto lastCalleeReg = fl.GetRegsSlotsCount() - calleeRegsMask.Count();
        auto lastCalleeVreg = fl.GetRegsSlotsCount() - fl.GetCalleeRegistersCount(false) - calleeVregsMask.Count();
        GetMasm()->LoadCPURegList(calleeRegs, MemOperand(VixlReg(sp), VixlImm(lastCalleeReg * fl.GetSlotSize())));
        GetMasm()->LoadCPURegList(calleeVregs, MemOperand(VixlReg(sp), VixlImm(lastCalleeVreg * fl.GetSlotSize())));
    } else {
        // SP either points to the frame's top or frame's top + FPLR slot
        auto spToRegsSlots = CFrameLayout::GetTopToRegsSlotsCount();
        if (frameInfo.GetSaveFrameAndLinkRegs() || ProvideCFI()) {
            // Adjust for FPLR slot
            spToRegsSlots -= CFrameLayout::GetFpLrSlotsCount();
        }
        auto lastCalleeReg = spToRegsSlots + calleeRegsMask.Count();
        auto lastCalleeVreg = spToRegsSlots + fl.GetCalleeRegistersCount(false) + calleeVregsMask.Count();
        GetMasm()->LoadCPURegList(calleeRegs, MemOperand(VixlReg(sp), VixlImm(-lastCalleeReg * fl.GetSlotSize())));
        GetMasm()->LoadCPURegList(calleeVregs, MemOperand(VixlReg(sp), VixlImm(-lastCalleeVreg * fl.GetSlotSize())));
    }
    SET_CFI_OFFSET(popCallees, encoder->GetCursorOffset());

    // Adjust SP
    if (frameInfo.GetAdjustSpReg()) {
        // SP points to the frame's bottom
        auto spToFrameTopSlots = fl.GetRegsSlotsCount() + CFrameRegs::Start() - CFrameReturnAddr::Start();
        if (frameInfo.GetSaveFrameAndLinkRegs() || ProvideCFI()) {
            spToFrameTopSlots -= CFrameLayout::GetFpLrSlotsCount();
        }
        auto spToFrameTopOffset = spToFrameTopSlots * fl.GetSlotSize();
        encoder->EncodeAdd(sp, sp, Imm(spToFrameTopOffset));
    }

    // Restore FP and LR
    if (IsOsrMode()) {
        encoder->EncodeAdd(sp, sp, Imm(CFrameLayout::GetFpLrSlotsCount() * fl.GetSlotSize()));
        encoder->EncodeLdp(fp, lr, false, MemRef(fp, -fl.GetOsrFpLrOffset()));
    } else if (frameInfo.GetSaveFrameAndLinkRegs() || ProvideCFI()) {
        GetMasm()->PopCPURegList(vixl::aarch64::CPURegList(VixlReg(fp), VixlReg(lr)));
    }
    SET_CFI_OFFSET(popFplr, encoder->GetCursorOffset());

    GetMasm()->Ret();
}
}  // namespace ark::compiler::aarch64
