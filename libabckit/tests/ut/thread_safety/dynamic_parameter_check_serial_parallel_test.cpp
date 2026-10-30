/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

// 20260807927972 Task 02-07: parameter_check scenario — serial vs 4-worker-parallel equivalence.
//
// The serial ApiModifier::Run() discovers each subclass's `handle` method (read-only enumeration:
// module import descriptor -> function insts -> DefineClassWithBuffer -> class method) and, per
// target, calls AddParamChecker (createGraphFromFunction D1 single-builder snapshot + per-graph
// bounds-check CFG surgery + functionSetGraph D4 prepare/commit + destroyGraph). Task 02-07 exposes
// that per-method work as two public entry points so a 4-worker pool can run it concurrently on
// distinct methods of the SAME shared file:
//   - CollectCheckerTasks: main-thread discovery (read-only; the inspect/enumerate API is not
//     hardened for concurrency).
//   - RunCheckerTask: the per-method bounds-check surgery run by workers (different methods ->
//     the D1/D4 shared-file paths are thread-safe; per-method Graph transforms are isolated).
//
// Run() is unchanged (still sequential, behavior-preserving); the existing serial parameter_check
// test stays green. Equivalence: serial Run() and the 4-worker pool produce the same deterministic
// modified output (out-of-bounds idx -> -1 instead of undefined).

#include "libabckit/c/abckit.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/metadata_core.h"
#include "libabckit/c/isa/isa_dynamic.h"

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"
#include "scenarios/parameter_check/dynamic/api_modifier.h"

#include <gtest/gtest.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

// NOLINTBEGIN(readability-magic-numbers)

namespace libabckit::test {

namespace {
auto g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);

const std::string DIR = std::string(ABCKIT_ABC_DIR) + "scenarios/parameter_check/dynamic/";
const std::string INPUT = DIR + "parameter_check.abc";
const std::string SERIAL_OUT = DIR + "parameter_check_serial.abc";
const std::string PARALLEL_OUT = DIR + "parameter_check_parallel.abc";
constexpr auto ENTRY = "parameter_check";

const MethodInfo METHOD = {"modules/base", "Handler", "handle"};

const std::string ORIGIN_EXPECTED =
    "str1\n"
    "str2\n"
    "str3\n"
    "undefined\n"
    "str3\n"
    "str2\n"
    "str4\n"
    "undefined\n";
const std::string MODIFIED_EXPECTED =
    "str1\n"
    "str2\n"
    "str3\n"
    "-1\n"
    "str3\n"
    "str2\n"
    "str4\n"
    "-1\n";
}  // namespace

class LibAbcKitDynamicScenarioParallelTest : public ::testing::Test {};

// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicScenarioParallelTest, LibAbcKitTestDynamicParameterCheckSerialParallel)
{
    // Origin: out-of-bounds idx -> undefined.
    auto origin = helpers::ExecuteDynamicAbc(INPUT, ENTRY);
    EXPECT_EQ(origin, ORIGIN_EXPECTED);

    // ---- SERIAL baseline: Run() over the whole file ----
    {
        AbckitFile *file = g_impl->openAbc(INPUT.c_str(), INPUT.size());
        ASSERT_NE(file, nullptr);
        ApiModifier mod(ABCKIT_VERSION_RELEASE_1_0_0, file, g_impl, METHOD);
        mod.Run();
        g_impl->writeAbc(mod.GetFile(), SERIAL_OUT.c_str(), SERIAL_OUT.size());
        g_impl->closeFile(file);
    }
    auto serialOut = helpers::ExecuteDynamicAbc(SERIAL_OUT, ENTRY);
    EXPECT_EQ(serialOut, MODIFIED_EXPECTED);

    // ---- PARALLEL: 4-worker pool over per-method tasks (same shared file) ----
    {
        AbckitFile *file = g_impl->openAbc(INPUT.c_str(), INPUT.size());
        ASSERT_NE(file, nullptr);
        ApiModifier mod(ABCKIT_VERSION_RELEASE_1_0_0, file, g_impl, METHOD);
        std::vector<AbckitCoreFunction *> tasks;
        mod.CollectCheckerTasks(tasks);  // main thread: discovery (read-only)
        ASSERT_FALSE(tasks.empty());

        std::atomic<size_t> next {0};
        std::vector<std::thread> threads;
        threads.reserve(4);
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&mod, &tasks, &next]() {
                for (;;) {
                    size_t idx = next.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= tasks.size()) {
                        break;
                    }
                    // D1 (createGraphFromFunction) + D4 (functionSetGraph) on a distinct method.
                    mod.RunCheckerTask(tasks[idx]);
                }
            });
        }
        for (auto &t : threads) {
            t.join();
        }
        g_impl->writeAbc(mod.GetFile(), PARALLEL_OUT.c_str(), PARALLEL_OUT.size());
        g_impl->closeFile(file);
    }
    auto parallelOut = helpers::ExecuteDynamicAbc(PARALLEL_OUT, ENTRY);
    EXPECT_EQ(parallelOut, MODIFIED_EXPECTED);

    // Serial == parallel (deterministic output).
    EXPECT_EQ(serialOut, parallelOut);
}

}  // namespace libabckit::test

// NOLINTEND(readability-magic-numbers)
