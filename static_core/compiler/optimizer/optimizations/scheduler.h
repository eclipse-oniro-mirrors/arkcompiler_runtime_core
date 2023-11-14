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

#ifndef COMPILER_OPTIMIZER_OPTIMIZATIONS_SCHEDULER_H_
#define COMPILER_OPTIMIZER_OPTIMIZATIONS_SCHEDULER_H_

#include "optimizer/ir/graph.h"
#include "compiler_options.h"

namespace panda::compiler {
class Scheduler : public Optimization {
public:
    explicit Scheduler(Graph *graph)
        : Optimization(graph),
          sched_(graph->GetLocalAllocator()->Adapter()),
          loads_(graph->GetLocalAllocator()->Adapter()),
          stores_(graph->GetLocalAllocator()->Adapter()),
          special_(graph->GetLocalAllocator()->Adapter()),
          ss_with_runtime_call_(graph->GetLocalAllocator()->Adapter()),
          old_(graph->GetLocalAllocator()->Adapter()),
          ocycle_(graph->GetLocalAllocator()->Adapter()),
          num_deps_(graph->GetLocalAllocator()->Adapter()),
          asap_(graph->GetLocalAllocator()->Adapter()),
          prio_(graph->GetLocalAllocator()->Adapter()),
          deps_(graph->GetLocalAllocator()->Adapter())
    {
    }

    NO_MOVE_SEMANTIC(Scheduler);
    NO_COPY_SEMANTIC(Scheduler);
    ~Scheduler() override = default;

    bool RunImpl() override;

    bool IsEnable() const override
    {
        return OPTIONS.IsCompilerScheduling();
    }

    const char *GetPassName() const override
    {
        return "Scheduler";
    }

private:
    void AddDep(uint32_t *prio, Inst *from, Inst *to, uint32_t latency, Inst *barrier);
    bool ScheduleBasicBlock(BasicBlock *bb);
    bool BuildAllDeps(BasicBlock *bb);

    void ProcessInst(Inst *inst, Marker mrk, uint32_t *num_inst, uint32_t *num_between, uint32_t *num_special,
                     Inst **last_barrier);
    void ProcessMemory(Inst *inst, uint32_t *prio, Inst *last_barrier);
    void ProcessSpecial(Inst *inst, uint32_t *prio, Inst *last_barrier);
    void ProcessRefInst(Inst *inst, uint32_t *prio, Inst *last_barrier);

    bool FinalizeBB(BasicBlock *bb, uint32_t cycle);
    void Cleanup();

    uint32_t ScheduleInstsBetweenBarriers(Inst *first, Inst *last);

    using SchedulerPriorityQueue = std::priority_queue<Inst *, InstVector, std::function<bool(Inst *, Inst *)>>;
    uint32_t SchedWithGlued(Inst *inst, SchedulerPriorityQueue *waiting, uint32_t cycle);

    uint32_t oprev_ {0};
    uint32_t num_barriers_ {0};
    uint32_t max_prio_ {0};

    InstVector sched_;
    InstVector loads_;
    InstVector stores_;
    InstVector special_;
    InstVector ss_with_runtime_call_;
    ArenaUnorderedMap<Inst *, uint32_t> old_;
    ArenaUnorderedMap<Inst *, uint32_t> ocycle_;
    ArenaUnorderedMap<Inst *, uint32_t> num_deps_;
    ArenaUnorderedMap<Inst *, uint32_t> asap_;
    ArenaUnorderedMap<Inst *, uint32_t> prio_;
    ArenaUnorderedMap<Inst *, ArenaUnorderedMap<Inst *, uint32_t>> deps_;
};
}  // namespace panda::compiler

#endif  // COMPILER_OPTIMIZER_OPTIMIZATIONS_SCHEDULER_H_
