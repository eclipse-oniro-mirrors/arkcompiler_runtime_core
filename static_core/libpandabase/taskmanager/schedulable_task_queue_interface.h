/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef PANDA_LIBPANDABASE_TASKMANAGER_SCHEDULABLE_TASK_QUEUE_INTERFACE_H
#define PANDA_LIBPANDABASE_TASKMANAGER_SCHEDULABLE_TASK_QUEUE_INTERFACE_H

#include "libpandabase/taskmanager/task_queue_interface.h"
#include <optional>

namespace panda::taskmanager::internal {

class SchedulableTaskQueueInterface : public TaskQueueInterface {
public:
    NO_COPY_SEMANTIC(SchedulableTaskQueueInterface);
    NO_MOVE_SEMANTIC(SchedulableTaskQueueInterface);

    /**
     * NewTasksCallback instance should be called after tasks adding. As argument you should input count of added
     * tasks.
     */
    using NewTasksCallback = std::function<void(TaskProperties, size_t)>;

    SchedulableTaskQueueInterface(TaskType task_type, VMType vm_type, uint8_t priority)
        : TaskQueueInterface(task_type, vm_type, priority)
    {
    }
    ~SchedulableTaskQueueInterface() override = default;
    /**
     * @brief Pops task from task queue. Operation is thread-safe. The method will wait new task if queue is empty and
     * method WaitForQueueEmptyAndFinish has not been executed. Otherwise it will return std::nullopt.
     */
    [[nodiscard]] virtual std::optional<Task> PopTask() = 0;

    /**
     * @brief Pops task from task queue with specified execution mode. Operation is thread-safe. The method will wait
     * new task if queue with specified execution mode is empty and method WaitForQueueEmptyAndFinish has not been
     * executed. Otherwise it will return std::nullopt.
     * @param mode - execution mode of task that we want to pop.
     */
    [[nodiscard]] virtual std::optional<Task> PopTask(TaskExecutionMode mode) = 0;

    /**
     * @brief This method sets the callback. It will be called after adding new task in AddTask method.
     * @param callback - function that get count of inputted tasks.
     */
    void virtual SetNewTasksCallback(NewTasksCallback callback) = 0;

    /// @brief Removes callback function.
    void virtual UnsetNewTasksCallback() = 0;
};

}  // namespace panda::taskmanager::internal

#endif  // PANDA_LIBPANDABASE_TASKMANAGER_SCHEDULABLE_TASK_QUEUE_INTERFACE_H