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

#include "intrinsics.h"
#include "plugins/ets/runtime/types/ets_sync_primitives.h"

namespace ark::ets::intrinsics {

void EtsMutexLock(EtsMutex *mutex)
{
    mutex->Lock();
}

void EtsMutexUnlock(EtsMutex *mutex)
{
    mutex->Unlock();
}

void EtsEventWait(EtsEvent *event)
{
    event->Wait();
}

void EtsEventFire(EtsEvent *event)
{
    event->Fire();
}

}  // namespace ark::ets::intrinsics