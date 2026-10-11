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

#ifndef BYTECODE_OPTIMIZER_RENUMBER_IC_SLOTS_H
#define BYTECODE_OPTIMIZER_RENUMBER_IC_SLOTS_H

#include "optimizer/pass.h"
#include "optimizer/ir/graph.h"

namespace ark::bytecodeopt {

class RenumberAnyIcSlots : public compiler::Optimization {
public:
    explicit RenumberAnyIcSlots(compiler::Graph *graph) : compiler::Optimization(graph) {}
    ~RenumberAnyIcSlots() override = default;
    NO_COPY_SEMANTIC(RenumberAnyIcSlots);
    NO_MOVE_SEMANTIC(RenumberAnyIcSlots);

    bool RunImpl() override;

    const char *GetPassName() const override
    {
        return "RenumberAnyIcSlots";
    }
};

}  // namespace ark::bytecodeopt

#endif  // BYTECODE_OPTIMIZER_RENUMBER_IC_SLOTS_H