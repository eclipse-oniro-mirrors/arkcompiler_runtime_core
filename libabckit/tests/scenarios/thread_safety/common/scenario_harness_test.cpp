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

#include "scenario_harness.h"

#include <algorithm>
#include <array>
#include <thread>

#include <gtest/gtest.h>

#include "helpers/helpers.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/isa/isa_dynamic.h"

namespace libabckit::test::scenario {
namespace {
struct Result {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    std::thread::id owner;
};
}  // namespace

TEST(AbckitScenarioHarnessTest, SerialRunsInlineAndParallelVisitsEveryTaskOnce)
{
    constexpr size_t TASKS = 11;
    for (size_t count : {1U, 4U}) {
        std::array<size_t, TASKS> visits {};
        auto caller = std::this_thread::get_id();
        auto run = RunShards<Result>(TASKS, count, [&](const auto &shard) {
            for (auto index : shard) {
                ++visits[index];
            }
            return Result {ABCKIT_STATUS_NO_ERROR, std::this_thread::get_id()};
        });
        ASSERT_EQ(run.status, ABCKIT_STATUS_NO_ERROR);
        ASSERT_EQ(run.workers.size(), count);
        EXPECT_TRUE(std::all_of(visits.begin(), visits.end(), [](auto visits) { return visits == 1; }));
        for (const auto &worker : run.workers) {
            EXPECT_EQ(worker.owner == caller, count == 1);
        }
        if (count == 4) {
            EXPECT_EQ(run.shardSizes, (std::vector<size_t> {3, 3, 3, 2}));
        }
    }
}

TEST(AbckitScenarioHarnessTest, FailureIsReportedAfterAllWorkersJoin)
{
    std::array<bool, 4> finished {};
    auto run = RunShards<Result>(4, 4, [&](const auto &shard) {
        finished[shard[0]] = true;
        return Result {shard[0] == 2 ? ABCKIT_STATUS_WRONG_STATE : ABCKIT_STATUS_NO_ERROR, std::this_thread::get_id()};
    });
    EXPECT_EQ(run.status, ABCKIT_STATUS_WRONG_STATE);
    EXPECT_TRUE(std::all_of(finished.begin(), finished.end(), [](bool done) { return done; }));
    auto invalid = RunShards<Result>(4, 0, [](const auto &) { return Result {}; });
    EXPECT_EQ(invalid.status, ABCKIT_STATUS_BAD_ARGUMENT);
}

TEST(AbckitScenarioHarnessTest, MoreWorkersThanTasksDoNotDuplicateWork)
{
    size_t visits = 0;
    auto run = RunShards<Result>(1, 4, [&](const auto &shard) {
        if (!shard.empty()) {
            ++visits;
        }
        return Result {};
    });
    EXPECT_EQ(run.status, ABCKIT_STATUS_NO_ERROR);
    EXPECT_EQ(visits, 1U);
    EXPECT_EQ(run.shardSizes, (std::vector<size_t> {1, 0, 0, 0}));
}

TEST(AbckitScenarioHarnessTest, NormalizeOnlyElapsedValueLines)
{
    EXPECT_EQ(NormalizeElapsedTime("body:42\nEllapsed time:\n123\nvalue:7\n"),
              "body:42\nEllapsed time:\n<elapsed-ms>\nvalue:7\n");
}

TEST(AbckitScenarioHarnessTest, GraphComparisonDetectsChangedStringOperand)
{
    constexpr auto INPUT = ABCKIT_ABC_DIR "ut/isa/isa_dynamic/load_string/load_string_dynamic.abc";
    const auto *inspect = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    const auto *modify = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    const auto *graphApi = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    ScopedFile file(Api()->openAbc(INPUT, std::char_traits<char>::length(INPUT)));
    ASSERT_NE(file.Get(), nullptr);
    auto *function = helpers::FindMethodByName(file.Get(), "load_string_dynamic.foo");
    ASSERT_NE(function, nullptr);
    Observation original;
    Observation modified;
    ASSERT_NO_FATAL_FAILURE(CaptureGraph(function, original));
    auto *graph = inspect->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    auto *load = helpers::FindFirstInst(graph, ABCKIT_ISA_API_DYNAMIC_OPCODE_LOADSTRING);
    if (load == nullptr) {
        Api()->destroyGraph(graph);
        FAIL() << "Expected string instruction";
    }
    auto *text = modify->createString(file.Get(), "comparison-mutated", 18);
    graphApi->iSetString(load, text);
    auto status = Api()->getLastError();
    if (status == ABCKIT_STATUS_NO_ERROR) {
        modify->functionSetGraph(function, graph);
        status = Api()->getLastError();
    }
    Api()->destroyGraph(graph);
    ASSERT_EQ(status, ABCKIT_STATUS_NO_ERROR);
    ASSERT_NO_FATAL_FAILURE(CaptureGraph(function, modified));
    ASSERT_EQ(original.graphs.size(), 1U);
    ASSERT_EQ(modified.graphs.size(), 1U);
    EXPECT_NE(original.graphs[0], modified.graphs[0]);
    EXPECT_EQ(file.Close(), ABCKIT_STATUS_NO_ERROR);
}
}  // namespace libabckit::test::scenario
