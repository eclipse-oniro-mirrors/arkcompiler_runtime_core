/**
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#include "renumber_ic_slots.h"
#include "optimizer/ir/inst.h"

namespace ark::bytecodeopt {
bool RenumberAnyIcSlots::RunImpl()
{
    bool isApplied = false;
    uint32_t slot = 0;
    for (auto bb : GetGraph()->GetBlocksLinearOrder()) {
        for (auto inst : bb->Insts()) {
            if (inst->GetOpcode() != compiler::Opcode::Intrinsic) {
                continue;
            }

            auto *intrinsic = inst->CastToIntrinsic();
            if (!compiler::IsCompilerAnyIntrinsic(intrinsic->GetIntrinsicId())) {
                continue;
            }

            uint8_t slotId =
                slot < compiler::INVALID_SLOT_ID ? static_cast<uint8_t>(slot++) : compiler::INVALID_SLOT_ID;
            isApplied |= intrinsic->GetSlotId() != slotId;
            intrinsic->SetSlotId(slotId);
        }
    }

    return isApplied;
}
}  // namespace ark::bytecodeopt
