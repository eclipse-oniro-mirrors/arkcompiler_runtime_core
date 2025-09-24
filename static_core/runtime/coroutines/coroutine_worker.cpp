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
#include "runtime/coroutines/coroutine_worker.h"
#include "runtime/include/thread_scopes.h"
#include "runtime/include/flattened_string_cache.h"
#include "runtime/include/thread-inl.h"
#include "mem/refstorage/global_object_storage.h"

namespace ark {

void CoroutineWorker::TriggerSchedulerExternally(Coroutine *requester)
{
    if (requester->GetType() == Coroutine::Type::MUTATOR && IsExternalSchedulingEnabled()) {
        os::memory::LockHolder l(posterLock_);
        if (extSchedulingPoster_ != nullptr) {
            extSchedulingPoster_->Post();
        }
    }
}

void CoroutineWorker::OnCoroBecameActive(Coroutine *co)
{
    TriggerSchedulerExternally(co);
}

void CoroutineWorker::DestroyCallbackPoster()
{
    os::memory::LockHolder l(posterLock_);
    if (extSchedulingPoster_ != nullptr) {
        extSchedulingPoster_.reset(nullptr);
    }
}

void CoroutineWorker::InitializeManagedStructures()
{
    ASSERT(!GetRuntime()->IsInitialized());
    CreateWorkerLocalObjects();
    CacheLocalObjectsInCoroutines();
}

void CoroutineWorker::OnWorkerStartup()
{
    ASSERT(Coroutine::GetCurrent()->GetWorker() == this);
    if (GetRuntime()->IsInitialized()) {
        CreateWorkerLocalObjects();
        CacheLocalObjectsInCoroutines();
        Coroutine::GetCurrent()->UpdateCachedObjects();
    }
}

void CoroutineWorker::CreateWorkerLocalObjects()
{
    ASSERT((GetLocalStorage().Get<CoroutineWorker::DataIdx::FLATTENED_STRING_CACHE, mem::Reference *>() == nullptr));
    auto *coro = Coroutine::GetCurrent();
    ASSERT(coro != nullptr);
    auto setFlattenedStringCache = [this, coro] {
        auto *flattenedStringCache = FlattenedStringCache::Create(GetPandaVM());
        ASSERT(flattenedStringCache != nullptr);
        auto *refStorage = coro->GetVM()->GetGlobalObjectStorage();
        auto *cacheRef = refStorage->Add(flattenedStringCache, mem::Reference::ObjectType::GLOBAL);
        GetLocalStorage().Set<CoroutineWorker::DataIdx::FLATTENED_STRING_CACHE>(
            cacheRef, [refStorage](void *ref) { refStorage->Remove(static_cast<mem::Reference *>(ref)); });
    };
    // We need to put the current coro into the managed state to be GC-safe, because we manipulate a raw
    // ObjectHeader*
    if (coro->IsInNativeCode()) {
        ScopedManagedCodeThread s(coro);
        setFlattenedStringCache();
    } else {
        setFlattenedStringCache();
    }
}

}  // namespace ark
