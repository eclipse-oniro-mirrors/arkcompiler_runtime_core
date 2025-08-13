/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "runtime/coroutines/coroutine_manager.h"
#include "runtime/coroutines/coroutine_worker_group.h"

namespace ark {

CoroutineWorkerGroup::Id CoroutineWorkerGroup::FromDomain(CoroutineManager *coroman, CoroutineWorkerDomain domain,
                                                          const PandaVector<CoroutineWorker::Id> &hint)
{
    ASSERT(coroman != nullptr);
    return coroman->GenerateWorkerGroupId(domain, hint);
}

}  // namespace ark
