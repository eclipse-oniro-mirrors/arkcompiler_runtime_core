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

#include "unit_test.h"

#include <algorithm>
#include <array>
#include <thread>

namespace ark::compiler {

class PassManagerTest : public GraphTest {};

TEST_F(PassManagerTest, ExecutionIdBelongsToManagerAndIsInheritedByChild)
{
    PassManager first(GetGraph(), nullptr);
    PassManager second(GetGraph(), nullptr);

    first.StartExecution();
    const auto firstId = first.GetExecutionCounter();
    second.StartExecution();

    EXPECT_NE(firstId, 0U);
    EXPECT_EQ(first.GetExecutionCounter(), firstId);
    EXPECT_NE(second.GetExecutionCounter(), firstId);

    PassManager child(GetGraph(), &first);
    EXPECT_EQ(child.GetExecutionCounter(), firstId);
}

TEST_F(PassManagerTest, ConcurrentTopLevelExecutionsReceiveUniqueIds)
{
    constexpr size_t MANAGER_COUNT = 8U;
    std::array<std::unique_ptr<PassManager>, MANAGER_COUNT> managers;
    std::array<std::thread, MANAGER_COUNT> workers;
    std::array<size_t, MANAGER_COUNT> ids {};
    for (auto &manager : managers) {
        manager = std::make_unique<PassManager>(GetGraph(), nullptr);
    }

    for (size_t index = 0; index < MANAGER_COUNT; ++index) {
        workers[index] = std::thread([&, index]() {
            managers[index]->StartExecution();
            ids[index] = managers[index]->GetExecutionCounter();
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }

    std::sort(ids.begin(), ids.end());
    EXPECT_NE(ids.front(), 0U);
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
}

TEST_F(PassManagerTest, InitialVisualizerDumpStartsTopLevelExecution)
{
    PassManager manager(GetGraph(), nullptr);
    ASSERT_EQ(manager.GetExecutionCounter(), 0U);

    manager.InitialDumpVisualizerGraph();

    EXPECT_NE(manager.GetExecutionCounter(), 0U);
}

}  // namespace ark::compiler
