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

#ifndef COMPILER_OPTIMIZER_OPTIMIZATIONS_LOOP_PEELING_H_
#define COMPILER_OPTIMIZER_OPTIMIZATIONS_LOOP_PEELING_H_

#include "optimizer/optimizations/loop_transform.h"
#include "compiler_options.h"
#include "optimizer/ir/analysis.h"

namespace panda::compiler {
class LoopPeeling : public LoopTransform<LoopExitPoint::LOOP_EXIT_HEADER> {
public:
    explicit LoopPeeling(Graph *graph) : LoopTransform(graph) {}
    NO_COPY_SEMANTIC(LoopPeeling);
    NO_MOVE_SEMANTIC(LoopPeeling);
    ~LoopPeeling() override = default;

    bool RunImpl() override;

    bool IsEnable() const override
    {
        return OPTIONS.IsCompilerLoopPeeling();
    }

    const char *GetPassName() const override
    {
        return "LoopPeeling";
    }

    void InvalidateAnalyses() override;

private:
    bool TransformLoop(Loop *loop) override;
    void InsertPreLoop(Loop *loop);
    size_t MoveLoopExitToBackEdge(BasicBlock *header, BasicBlock *back_edge);
    void UpdateClonedInstInputs(Inst *inst, BasicBlock *header, BasicBlock *back_edge);
    bool is_appied_ {false};
    SaveStateBridgesBuilder ssb_;
};
}  // namespace panda::compiler

#endif  // COMPILER_OPTIMIZER_OPTIMIZATIONS_LOOP_PEELING_H_
