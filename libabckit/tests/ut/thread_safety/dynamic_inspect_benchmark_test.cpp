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

#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"
#include "libabckit/c/abckit.h"
#include "libabckit/c/extensions/js/metadata_js.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/isa/isa_dynamic.h"
#include "libabckit/c/metadata_core.h"
#include "libabckit/src/adapter_dynamic/abckit_dynamic.h"
#include "libabckit/src/adapter_dynamic/metadata_modify_dynamic.h"
#include "libabckit/src/ir_impl.h"
#include "libabckit/src/metadata_inspect_impl.h"

namespace libabckit {
void InvalidateDynamicGraphCacheForStructuralChange(AbckitFile *file);
AbckitLiteral *FindOrCreateLiteralLiteralArrayDynamicImpl(AbckitFile *file, const std::string &value);
AbckitValue *FindOrCreateLiteralArrayValueDynamicImpl(AbckitFile *file, const std::string &value);
}  // namespace libabckit

namespace libabckit::test {

static auto g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_implI = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);

class LibAbcKitDynamicThreadSafetyTest : public ::testing::Test {};

struct InspectPerfStats {
    size_t moduleCount = 0;
    size_t functionCount = 0;
    size_t classCount = 0;
};

struct InspectPerfContext {
    InspectPerfStats *stats = nullptr;
    bool failed = false;
};

struct InspectPerfResult {
    InspectPerfStats stats;
    double elapsedMs = 0.0;
    bool success = false;
};

static bool CheckLastError(InspectPerfContext *ctx)
{
    if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        ctx->failed = true;
        return false;
    }
    return true;
}

static bool CheckStringForPerf(AbckitString *str, InspectPerfContext *ctx)
{
    auto *value = g_implI->abckitStringToString(str);
    if (value == nullptr || !CheckLastError(ctx) || value[0] == '\0') {
        ctx->failed = true;
        return false;
    }
    return true;
}

static bool PerfFunctionVisitor(AbckitCoreFunction *function, void *data)
{
    auto *ctx = reinterpret_cast<InspectPerfContext *>(data);
    auto *name = g_implI->functionGetName(function);
    if (name == nullptr || !CheckLastError(ctx) || !CheckStringForPerf(name, ctx)) {
        return false;
    }
    ++ctx->stats->functionCount;
    return true;
}

static bool PerfClassVisitor(AbckitCoreClass *klass, void *data)
{
    auto *ctx = reinterpret_cast<InspectPerfContext *>(data);
    auto *name = g_implI->classGetName(klass);
    if (name == nullptr || !CheckLastError(ctx) || !CheckStringForPerf(name, ctx)) {
        return false;
    }
    ++ctx->stats->classCount;

    if (!g_implI->classEnumerateMethods(klass, data, PerfFunctionVisitor)) {
        ctx->failed = true;
        return false;
    }
    return CheckLastError(ctx);
}

static bool PerfModuleVisitor(AbckitCoreModule *module, void *data)
{
    auto *ctx = reinterpret_cast<InspectPerfContext *>(data);
    auto *file = g_implI->moduleGetFile(module);
    if (file == nullptr || !CheckLastError(ctx)) {
        return false;
    }

    auto *name = g_implI->moduleGetName(module);
    if (name == nullptr || !CheckLastError(ctx) || !CheckStringForPerf(name, ctx)) {
        return false;
    }
    ++ctx->stats->moduleCount;

    if (!g_implI->moduleEnumerateTopLevelFunctions(module, data, PerfFunctionVisitor)) {
        ctx->failed = true;
        return false;
    }
    if (!CheckLastError(ctx)) {
        return false;
    }

    if (!g_implI->moduleEnumerateClasses(module, data, PerfClassVisitor)) {
        ctx->failed = true;
        return false;
    }
    return CheckLastError(ctx);
}

static bool RunInspectTraversal(AbckitFile *file, size_t iterations, InspectPerfStats *stats)
{
    InspectPerfContext ctx {stats, false};
    for (size_t iter = 0; iter < iterations && !ctx.failed; ++iter) {
        if (!g_implI->fileEnumerateModules(file, &ctx, PerfModuleVisitor)) {
            ctx.failed = true;
            return false;
        }
        if (!CheckLastError(&ctx)) {
            return false;
        }
    }
    return !ctx.failed;
}

static double GetElapsedMs(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

static InspectPerfResult MeasureSingleThreadInspect(AbckitFile *file, size_t totalIterations)
{
    InspectPerfResult result;
    auto start = std::chrono::steady_clock::now();
    result.success = RunInspectTraversal(file, totalIterations, &result.stats);
    auto end = std::chrono::steady_clock::now();
    result.elapsedMs = GetElapsedMs(start, end);
    return result;
}

static InspectPerfResult MeasureParallelInspect(AbckitFile *file, size_t threadCount, size_t iterationsPerThread)
{
    InspectPerfResult result;
    std::atomic<bool> start {false};
    std::atomic<bool> failed {false};
    std::vector<InspectPerfStats> threadStats(threadCount);
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (size_t i = 0; i < threadCount; ++i) {
        threads.emplace_back([&, i]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            if (!RunInspectTraversal(file, iterationsPerThread, &threadStats[i])) {
                failed.store(true, std::memory_order_relaxed);
            }
        });
    }

    auto begin = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    for (auto &thread : threads) {
        thread.join();
    }
    auto end = std::chrono::steady_clock::now();

    for (const auto &stats : threadStats) {
        result.stats.moduleCount += stats.moduleCount;
        result.stats.functionCount += stats.functionCount;
        result.stats.classCount += stats.classCount;
    }
    result.elapsedMs = GetElapsedMs(begin, end);
    result.success = !failed.load(std::memory_order_relaxed);
    return result;
}

static size_t GetInspectPerfIterations()
{
    constexpr size_t DEFAULT_ITERATIONS_PER_THREAD = 2048;
    auto *value = std::getenv("ABCKIT_INSPECT_PERF_ITERATIONS");
    if (value == nullptr) {
        return DEFAULT_ITERATIONS_PER_THREAD;
    }
    auto parsed = std::strtoull(value, nullptr, 10);
    return parsed == 0 ? DEFAULT_ITERATIONS_PER_THREAD : static_cast<size_t>(parsed);
}

// Test: test-kind=api, api=InspectApiImpl::fileEnumerateModules, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DISABLED_DynamicCoreInspectTraversalPerf)
{
    const char *abcPath = std::getenv("ABCKIT_INSPECT_PERF_ABC");
    if (abcPath == nullptr) {
        abcPath = ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/classes/classes_dynamic.abc";
    }

    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(abcPath, &file);

    InspectPerfStats warmupStats;
    ASSERT_TRUE(RunInspectTraversal(file, 1, &warmupStats));
    ASSERT_GT(warmupStats.moduleCount, 0);
    ASSERT_GT(warmupStats.functionCount, 0);
    ASSERT_GT(warmupStats.classCount, 0);

    constexpr std::array<size_t, 3> THREAD_COUNTS {1, 4, 8};
    auto iterationsPerThread = GetInspectPerfIterations();

    std::fprintf(stdout, "[libabckit inspect perf] abc=%s, iterations_per_thread=%zu\n", abcPath, iterationsPerThread);
    std::fprintf(stdout, "threads,total_traversals,single_ms,parallel_ms,speedup,modules,functions,classes\n");

    for (auto threadCount : THREAD_COUNTS) {
        auto totalIterations = threadCount * iterationsPerThread;
        auto single = MeasureSingleThreadInspect(file, totalIterations);
        auto parallel = MeasureParallelInspect(file, threadCount, iterationsPerThread);
        ASSERT_TRUE(single.success);
        ASSERT_TRUE(parallel.success);
        ASSERT_EQ(single.stats.moduleCount, parallel.stats.moduleCount);
        ASSERT_EQ(single.stats.functionCount, parallel.stats.functionCount);
        ASSERT_EQ(single.stats.classCount, parallel.stats.classCount);
        ASSERT_GT(parallel.elapsedMs, 0.0);

        auto speedup = single.elapsedMs / parallel.elapsedMs;
        std::fprintf(stdout, "%zu,%zu,%.6f,%.6f,%.6f,%zu,%zu,%zu\n", threadCount, totalIterations, single.elapsedMs,
                     parallel.elapsedMs, speedup, parallel.stats.moduleCount, parallel.stats.functionCount,
                     parallel.stats.classCount);
        RecordProperty("inspect_parallel_" + std::to_string(threadCount) + "_threads_speedup", std::to_string(speedup));
    }

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::fileEnumerateModules, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DISABLED_StaticCoreInspectTraversalPerf)
{
    const char *abcPath = std::getenv("ABCKIT_INSPECT_PERF_ABC");
    if (abcPath == nullptr) {
        abcPath = ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/fields/fields_static.abc";
    }

    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(abcPath, &file);
    ASSERT_EQ(file->frontend, Mode::STATIC);

    InspectPerfStats warmupStats;
    ASSERT_TRUE(RunInspectTraversal(file, 1U, &warmupStats));
    ASSERT_GT(warmupStats.functionCount, 0U);

    const auto iterations = GetInspectPerfIterations();
    const auto measured = MeasureSingleThreadInspect(file, iterations);
    ASSERT_TRUE(measured.success);
    std::fprintf(stdout,
                 "[libabckit static inspect perf] abc=%s,iterations=%zu,elapsed_ms=%.6f,modules=%zu,functions=%zu,"
                 "classes=%zu\n",
                 abcPath, iterations, measured.elapsedMs, measured.stats.moduleCount, measured.stats.functionCount,
                 measured.stats.classCount);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

}  // namespace libabckit::test
