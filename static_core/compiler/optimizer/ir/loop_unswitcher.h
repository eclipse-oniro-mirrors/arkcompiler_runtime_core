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

#ifndef COMPILER_OPTIMIZER_IR_LOOP_UNSWITCHER_H_
#define COMPILER_OPTIMIZER_IR_LOOP_UNSWITCHER_H_

#include "graph_cloner.h"

namespace panda::compiler {
/**
 * Class unswitchs loop:
 * - Clone loop;
 * - Fix control & data flow;
 */
class LoopUnswitcher : public GraphCloner {
public:
    static Inst *FindUnswitchInst(Loop *loop);
    static bool IsSmallLoop(Loop *loop);
    static void EstimateInstructionsCount(const Loop *loop, const Inst *unswitch_inst, uint32_t *loop_size,
                                          uint32_t *true_count, uint32_t *false_count);
    explicit LoopUnswitcher(Graph *graph, ArenaAllocator *allocator, ArenaAllocator *local_allocator);
    Loop *UnswitchLoop(Loop *loop, Inst *inst);

private:
    LoopClonerData *PrepareLoopToUnswitch(Loop *loop);
    void BuildLoopUnswitchControlFlow(LoopClonerData *unswitch_data);
    void BuildLoopUnswitchDataFlow(LoopClonerData *unswitch_data, Inst *if_inst);
    void ReplaceWithConstantCondition(Inst *if_inst);
    ArenaVector<Inst *> conditions_;
};
}  // namespace panda::compiler

#endif
