/**
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
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

#ifndef PANDA_RUNTIME_COROUTINES_COROUTINE_WORKER_H
#define PANDA_RUNTIME_COROUTINES_COROUTINE_WORKER_H

#include "include/external_callback_poster.h"
#include "runtime/include/runtime.h"
#include "runtime/coroutines/local_storage.h"

namespace ark {

/**
 * THE ORDER HAS MEANING
 * ASCEDING ORDER - HIGHER PRIORITY
 * DO NOT CHANGE INITIALIZATION VALUES
 */
enum class CoroutinePriority {
    LOW_PRIORITY,
    MEDIUM_PRIORITY,
    DEFAULT_PRIORITY = MEDIUM_PRIORITY,
    HIGH_PRIORITY,
    CRITICAL_PRIORITY,
    PRIORITY_COUNT
};

/// Represents a coroutine worker, which can host multiple coroutines and schedule them.
class CoroutineWorker {
public:
    enum class DataIdx { INTEROP_CTX_PTR, EXTERNAL_IFACES, FLATTENED_STRING_CACHE, LAST_ID };
    using LocalStorage = StaticLocalStorage<DataIdx>;
    using Id = int32_t;

    static constexpr Id INVALID_ID = -1;

    NO_COPY_SEMANTIC(CoroutineWorker);
    NO_MOVE_SEMANTIC(CoroutineWorker);

    CoroutineWorker(Runtime *runtime, PandaVM *vm, PandaString name, Id id, bool isMainWorker)
        : runtime_(runtime), vm_(vm), name_(std::move(name)), id_(id), isMainWorker_(isMainWorker)
    {
    }
    virtual ~CoroutineWorker()
    {
        DestroyCallbackPoster();
    }

    Runtime *GetRuntime()
    {
        return runtime_;
    }

    PandaVM *GetPandaVM() const
    {
        return vm_;
    }

    Id GetId() const
    {
        return id_;
    }

    PandaString GetName() const
    {
        return name_;
    }

    void SetName(PandaString name)
    {
        name_ = std::move(name);
    }

    bool IsMainWorker() const
    {
        return isMainWorker_;
    }

    LocalStorage &GetLocalStorage()
    {
        return localStorage_;
    }

    void SetCallbackPoster(PandaUniquePtr<CallbackPoster> poster)
    {
        ASSERT(!extSchedulingPoster_);
        extSchedulingPoster_ = std::move(poster);
    }

    void DestroyCallbackPoster();

    bool IsExternalSchedulingEnabled() const
    {
        return extSchedulingPoster_ != nullptr;
    }

    void OnCoroBecameActive(Coroutine *co);

    void TriggerSchedulerExternally(Coroutine *requester);

    /// should be called once the VM is ready to create managed objects in the managed heap
    void InitializeManagedStructures();

    /// should be called from CoroutineManager after worker creation from the coroutine assigned to the worker
    void OnWorkerStartup();

private:
    void CreateWorkerLocalObjects();
    virtual void CacheLocalObjectsInCoroutines() {}

private:
    Runtime *runtime_ = nullptr;
    PandaVM *vm_ = nullptr;
    PandaString name_;
    Id id_ = INVALID_ID;
    bool isMainWorker_ = false;
    LocalStorage localStorage_;
    // event loop poster
    os::memory::Mutex posterLock_;
    PandaUniquePtr<CallbackPoster> extSchedulingPoster_;
};

}  // namespace ark

#endif /* PANDA_RUNTIME_COROUTINES_COROUTINE_WORKER_H */
