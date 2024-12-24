/**
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_EXTERNAL_IFACE_TABLE_H_
#define PANDA_PLUGINS_ETS_RUNTIME_EXTERNAL_IFACE_TABLE_H_

#include <functional>

#include "runtime/interpreter/frame.h"
#include "plugins/ets/runtime/job_queue.h"

namespace ark::ets {

class ExternalIfaceTable {
public:
    using ClearInteropHandleScopesFunction = std::function<void(Frame *)>;

    NO_COPY_SEMANTIC(ExternalIfaceTable);
    NO_MOVE_SEMANTIC(ExternalIfaceTable);

    ExternalIfaceTable() = default;
    virtual ~ExternalIfaceTable() = default;

    JobQueue *GetJobQueue() const
    {
        return jobQueue_.get();
    }

    void SetJobQueue(PandaUniquePtr<JobQueue> jobQueue)
    {
        jobQueue_ = std::move(jobQueue);
    }

    const ClearInteropHandleScopesFunction &GetClearInteropHandleScopesFunction() const
    {
        return clearInteropHandleScopes_;
    }

    void SetClearInteropHandleScopesFunction(const ClearInteropHandleScopesFunction &func)
    {
        clearInteropHandleScopes_ = func;
    }

private:
    PandaUniquePtr<JobQueue> jobQueue_ = nullptr;
    ClearInteropHandleScopesFunction clearInteropHandleScopes_ = nullptr;
};

}  // namespace ark::ets
#endif  // #ifndef PANDA_PLUGINS_ETS_RUNTIME_EXTERNAL_IFACE_TABLE_H_