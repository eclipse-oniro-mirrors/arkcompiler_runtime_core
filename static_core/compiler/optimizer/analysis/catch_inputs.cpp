/**
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#include "optimizer/analysis/catch_inputs.h"
#include "optimizer/ir/basicblock.h"
#include "optimizer/ir/inst.h"
#include "optimizer/ir/graph.h"

namespace panda::compiler {
namespace {
bool IsCatch(BasicBlock *bb)
{
    return bb->IsCatch() || bb->IsCatchBegin() || bb->IsCatchEnd();
}

void ProcessInst(Inst *inst, Marker visited)
{
    for (auto &input : inst->GetInputs()) {
        auto input_inst = inst->GetDataFlowInput(input.GetInst());
        // mark only instructions defined in non-catch blocks
        auto bb = input_inst->GetBasicBlock();
        if (!IsCatch(bb)) {
            input_inst->SetFlag(inst_flags::Flags::CATCH_INPUT);
        }
        if (input_inst->IsPhi() && !input_inst->SetMarker(visited)) {
            ProcessInst(input_inst, visited);
        }
    }
}

void ProcessBlock(BasicBlock *block, Marker visited)
{
    for (auto inst : block->AllInstsSafe()) {
        ProcessInst(inst, visited);
    }
}
}  // namespace

bool CatchInputs::RunImpl()
{
    MarkerHolder holder {GetGraph()};
    Marker visited = holder.GetMarker();
    for (auto block : GetGraph()->GetVectorBlocks()) {
        if (block == nullptr || !IsCatch(block)) {
            continue;
        }

        ProcessBlock(block, visited);
    }
    return true;
}
}  // namespace panda::compiler
