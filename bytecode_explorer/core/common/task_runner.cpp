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

#include "core/common/task_runner.h"

#include <utility>

namespace panda::bytecode_explorer {

TaskRunner::TaskRunner() : worker_([this] { WorkerLoop(); }) {}

TaskRunner::~TaskRunner()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    workCondition_.notify_one();
    worker_.join();
}

void TaskRunner::Post(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        tasks_.push_back(std::move(task));
    }
    workCondition_.notify_one();
}

void TaskRunner::WaitForIdle()
{
    std::unique_lock<std::mutex> lock(mutex_);
    idleCondition_.wait(lock, [this] { return tasks_.empty() && !active_; });
}

void TaskRunner::WorkerLoop()
{
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            workCondition_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
            active_ = true;
        }

        task();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_ = false;
            if (tasks_.empty()) {
                idleCondition_.notify_all();
            }
        }
    }
}

}  // namespace panda::bytecode_explorer
