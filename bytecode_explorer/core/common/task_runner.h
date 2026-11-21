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

#ifndef BYTECODE_EXPLORER_CORE_COMMON_TASK_RUNNER_H
#define BYTECODE_EXPLORER_CORE_COMMON_TASK_RUNNER_H

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace panda::bytecode_explorer {

class TaskRunner final {
public:
    TaskRunner();
    ~TaskRunner();

    TaskRunner(const TaskRunner &) = delete;
    TaskRunner &operator=(const TaskRunner &) = delete;
    TaskRunner(TaskRunner &&) = delete;
    TaskRunner &operator=(TaskRunner &&) = delete;

    void Post(std::function<void()> task);
    void WaitForIdle();

private:
    void WorkerLoop();

    std::mutex mutex_;
    std::condition_variable workCondition_;
    std::condition_variable idleCondition_;
    std::deque<std::function<void()>> tasks_;
    bool stopping_ {false};
    bool active_ {false};
    std::thread worker_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_COMMON_TASK_RUNNER_H
