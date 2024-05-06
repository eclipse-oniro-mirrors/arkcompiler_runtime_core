/*
 * Copyright (c) 2021-2024 Huawei Device Co., Ltd.
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

#include "pipeline.h"
#include "compiler_options.h"
#include "inplace_task_runner.h"
#include "background_task_runner.h"
#include "compiler_task_runner.h"

#include "optimizer/code_generator/codegen.h"
#include "optimizer/code_generator/codegen_native.h"
#include "optimizer/code_generator/method_properties.h"
#include "optimizer/ir/graph.h"
#include "optimizer/ir/visualizer_printer.h"
#include "optimizer/analysis/alias_analysis.h"
#include "optimizer/analysis/linear_order.h"
#include "optimizer/analysis/monitor_analysis.h"
#include "optimizer/analysis/rpo.h"
#include "optimizer/optimizations/balance_expressions.h"
#include "optimizer/optimizations/branch_elimination.h"
#include "optimizer/optimizations/checks_elimination.h"
#include "optimizer/optimizations/code_sink.h"
#include "optimizer/optimizations/deoptimize_elimination.h"
#include "optimizer/optimizations/cleanup.h"
#include "optimizer/optimizations/escape.h"
#include "optimizer/optimizations/if_conversion.h"
#include "optimizer/optimizations/inlining.h"
#include "optimizer/optimizations/licm.h"
#include "optimizer/optimizations/licm_conditions.h"
#include "optimizer/optimizations/loop_idioms.h"
#include "optimizer/optimizations/loop_peeling.h"
#include "optimizer/optimizations/loop_unswitch.h"
#include "optimizer/optimizations/loop_unroll.h"
#include "optimizer/optimizations/lowering.h"
#include "optimizer/optimizations/lse.h"
#include "optimizer/optimizations/memory_barriers.h"
#include "optimizer/optimizations/memory_coalescing.h"
#include "optimizer/optimizations/optimize_string_concat.h"
#include "optimizer/optimizations/peepholes.h"
#include "optimizer/optimizations/phi_type_resolving.h"
#include "optimizer/optimizations/redundant_loop_elimination.h"
#include "optimizer/optimizations/regalloc/reg_alloc.h"
#include "optimizer/optimizations/reserve_string_builder_buffer.h"
#include "optimizer/optimizations/savestate_optimization.h"
#include "optimizer/optimizations/scheduler.h"
#include "optimizer/optimizations/simplify_string_builder.h"
#include "optimizer/optimizations/try_catch_resolving.h"
#include "optimizer/optimizations/inline_intrinsics.h"
#include "optimizer/optimizations/vn.h"
#include "optimizer/optimizations/cse.h"
#include "optimizer/optimizations/move_constants.h"
#include "optimizer/optimizations/adjust_arefs.h"
#include "optimizer/optimizations/if_merging.h"

#include "compiler/generated/pipeline_includes.h"

namespace ark::compiler {

std::unique_ptr<Pipeline> Pipeline::Create(Graph *graph)
{
    switch (graph->GetLanguage()) {
#include "compiler/generated/create_pipeline.inl"
        default:
            return std::make_unique<Pipeline>(graph);
    }
}

static inline bool RunCodegenPass(Graph *graph)
{
    if (graph->GetMethodProperties().GetRequireFrameSetup()) {
        return graph->RunPass<Codegen>();
    }
    return graph->RunPass<CodegenNative>();
}

/* static */
template <TaskRunnerMode RUNNER_MODE>
void Pipeline::Run(CompilerTaskRunner<RUNNER_MODE> taskRunner)
{
    auto pipeline = taskRunner.GetContext().GetPipeline();
    auto *graph = pipeline->GetGraph();
#if !defined(NDEBUG) && !defined(PANDA_TARGET_MOBILE)
    if (g_options.IsCompilerVisualizerDump()) {
        graph->GetPassManager()->InitialDumpVisualizerGraph();
    }
#endif  // NDEBUG && PANDA_TARGET_MOBILE

    taskRunner.AddFinalize(
        [](CompilerContext<RUNNER_MODE> &compilerCtx) { compilerCtx.GetGraph()->GetPassManager()->Finalize(); });

    if (g_options.WasSetCompilerRegallocRegMask()) {
        COMPILER_LOG(DEBUG, REGALLOC) << "Regalloc mask force set to " << std::hex
                                      << g_options.GetCompilerRegallocRegMask() << "\n";
        graph->SetArchUsedRegs(g_options.GetCompilerRegallocRegMask());
    }

    if (!g_options.IsCompilerNonOptimizing()) {
        taskRunner.SetTaskOnSuccess([](CompilerTaskRunner<RUNNER_MODE> nextRunner) {
            Pipeline::RunRegAllocAndCodeGenPass<RUNNER_MODE>(std::move(nextRunner));
        });
        bool success = pipeline->RunOptimizations();
        CompilerTaskRunner<RUNNER_MODE>::EndTask(std::move(taskRunner), success);
        return;
    }
    // TryCatchResolving is needed in the non-optimizing mode since it removes unreachable for compiler
    // catch-handlers; After supporting catch-handlers' compilation, this pass can be run in the optimizing mode
    // only.
    graph->template RunPass<TryCatchResolving>();
    if (!graph->template RunPass<MonitorAnalysis>()) {
        LOG(WARNING, COMPILER) << "Compiler detected incorrect monitor policy";
        CompilerTaskRunner<RUNNER_MODE>::EndTask(std::move(taskRunner), false);
        return;
    }
    Pipeline::RunRegAllocAndCodeGenPass<RUNNER_MODE>(std::move(taskRunner));
}

/* static */
template <TaskRunnerMode RUNNER_MODE>
void Pipeline::RunRegAllocAndCodeGenPass(CompilerTaskRunner<RUNNER_MODE> taskRunner)
{
    auto *graph = taskRunner.GetContext().GetPipeline()->GetGraph();
    bool fatalOnErr = !g_options.IsCompilerAllowBackendFailures();

    // Avoid spending too much time in RegAlloc:
    auto estimatedSize = graph->EstimateCodeSize();
    if (estimatedSize > g_options.GetCompilerMaxGenCodeSize()) {
        if (fatalOnErr) {
            LOG(FATAL, COMPILER) << "RunOptimizations failed: predicted code size is too big (" << estimatedSize << ")";
        }
        CompilerTaskRunner<RUNNER_MODE>::EndTask(std::move(taskRunner), false);
        return;
    }
    graph->template RunPass<Cleanup>();

    taskRunner.SetTaskOnSuccess([fatalOnErr](CompilerTaskRunner<RUNNER_MODE> nextRunner) {
        nextRunner.AddCallbackOnFail([fatalOnErr]([[maybe_unused]] CompilerContext<RUNNER_MODE> &compilerCtx) {
            if (fatalOnErr) {
                LOG(FATAL, COMPILER) << "RunOptimizations failed: code generation error";
            }
        });
        bool success = RunCodegenPass(nextRunner.GetContext().GetPipeline()->GetGraph());
        CompilerTaskRunner<RUNNER_MODE>::EndTask(std::move(nextRunner), success);
    });
    bool success = RegAlloc(graph);
    if (!success && fatalOnErr) {
        LOG(FATAL, COMPILER) << "RunOptimizations failed: register allocation error";
    }
    CompilerTaskRunner<RUNNER_MODE>::EndTask(std::move(taskRunner), success);
}

bool Pipeline::RunOptimizations()
{
    auto graph = GetGraph();

    /* peepholer and branch elimination have some parts that have
     * to be delayed up until loop unrolling is done, however, if
     * loop unrolling is not going to be run we don't have to delay */
    if (!g_options.IsCompilerLoopUnroll()) {
        graph->SetUnrollComplete();
    }
    graph->RunPass<Peepholes>();
    graph->RunPass<BranchElimination>();
    graph->RunPass<OptimizeStringConcat>();
    graph->RunPass<SimplifyStringBuilder>();

    // The problem with inlining in OSR mode can be found in `bitops-nsieve-bits` benchmark and it is in the
    // following: we inline the method that has user X within a loop, then peepholes optimize datflow and def of
    // the X become another instruction within inlined method, but SaveStateOsr didn't take it into account, thus,
    // we don't restore value of this new definition.
    // NOTE(msherstennikov): find way to inline in OSR mode
    if (!graph->IsOsrMode()) {
        graph->RunPass<Inlining>();
    }
    graph->RunPass<CatchInputs>();
    graph->RunPass<TryCatchResolving>();
    if (!graph->RunPass<MonitorAnalysis>()) {
        LOG(WARNING, COMPILER) << "Compiler detected incorrect monitor policy";
        return false;
    }
    graph->RunPass<Peepholes>();
    graph->RunPass<BranchElimination>();
    graph->RunPass<ValNum>();
    graph->RunPass<IfMerging>();
    graph->RunPass<Cleanup>(false);
    graph->RunPass<Peepholes>();
    if (graph->IsAotMode()) {
        graph->RunPass<Cse>();
    }
    if (graph->IsDynamicMethod()) {
        graph->RunPass<InlineIntrinsics>();
        graph->RunPass<PhiTypeResolving>();
        graph->RunPass<Peepholes>();
        graph->RunPass<BranchElimination>();
        graph->RunPass<ValNum>();
        graph->RunPass<Cleanup>(false);
    }
    graph->RunPass<ChecksElimination>();
    graph->RunPass<Licm>(g_options.GetCompilerLicmHoistLimit());
    graph->RunPass<LicmConditions>();
    graph->RunPass<RedundantLoopElimination>();
    graph->RunPass<LoopPeeling>();
    graph->RunPass<LoopUnswitch>(g_options.GetCompilerLoopUnswitchMaxLevel(),
                                 g_options.GetCompilerLoopUnswitchMaxInsts());
    graph->RunPass<Lse>();
    graph->RunPass<ValNum>();
    if (graph->RunPass<Peepholes>() && graph->RunPass<BranchElimination>()) {
        graph->RunPass<Peepholes>();
        graph->RunPass<ValNum>();
    }
    graph->RunPass<Cleanup>();
    if (graph->IsAotMode()) {
        graph->RunPass<Cse>();
    }
    graph->RunPass<LoopIdioms>();
    graph->RunPass<ChecksElimination>();
    if (graph->RunPass<DeoptimizeElimination>()) {
        graph->RunPass<Peepholes>();
    }
    graph->RunPass<LoopUnroll>(g_options.GetCompilerLoopUnrollInstLimit(), g_options.GetCompilerLoopUnrollFactor());
    OptimizationsAfterUnroll(graph);
    graph->RunPass<EscapeAnalysis>();
    graph->RunPass<ReserveStringBuilderBuffer>();

    /* to be removed once generic loop unrolling is implemented */
    ASSERT(graph->IsUnrollComplete());

    graph->RunPass<Peepholes>();
    graph->RunPass<BranchElimination>();
    graph->RunPass<BalanceExpressions>();
    graph->RunPass<ValNum>();
    if (graph->IsAotMode()) {
        graph->RunPass<Cse>();
    }
    graph->RunPass<SaveStateOptimization>();
    graph->RunPass<Peepholes>();
#ifndef NDEBUG
    graph->SetLowLevelInstructionsEnabled();
#endif  // NDEBUG
    graph->RunPass<Cleanup>(false);
    graph->RunPass<Lowering>();
    graph->RunPass<Cleanup>(false);
    graph->RunPass<CodeSink>();
    graph->RunPass<MemoryCoalescing>(g_options.IsCompilerMemoryCoalescingAligned());
    graph->RunPass<IfConversion>(g_options.GetCompilerIfConversionLimit());
    graph->RunPass<Scheduler>();
    // Perform MoveConstants after Scheduler because Scheduler can rearrange constants
    // and cause spillfill in reg alloc
    graph->RunPass<MoveConstants>();
    if (graph->RunPass<AdjustRefs>()) {
        graph->RunPass<ValNum>();
        graph->RunPass<Cleanup>(false);
    }
    graph->RunPass<OptimizeMemoryBarriers>();

    return true;
}

template void Pipeline::Run<BACKGROUND_MODE>(CompilerTaskRunner<BACKGROUND_MODE>);
template void Pipeline::Run<INPLACE_MODE>(CompilerTaskRunner<INPLACE_MODE>);
template void Pipeline::RunRegAllocAndCodeGenPass<BACKGROUND_MODE>(CompilerTaskRunner<BACKGROUND_MODE>);
template void Pipeline::RunRegAllocAndCodeGenPass<INPLACE_MODE>(CompilerTaskRunner<INPLACE_MODE>);

}  // namespace ark::compiler
