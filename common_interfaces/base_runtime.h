/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef COMMON_INTERFACES_BASE_RUNTIME_H
#define COMMON_INTERFACES_BASE_RUNTIME_H

#include <atomic>
#include <functional>
#include <mutex>

#include "base/runtime_param.h"

namespace common {
class BaseStringTableImpl;
template <typename Impl>
class BaseStringTableInterface;
class BaseObject;
class HeapManager;
class MutatorManager;
class ThreadHolderManager;
class ThreadHolder;
class BaseClassRoots;

enum class GcType : uint8_t {
    ASYNC,
    SYNC,
    FULL,  // Waiting finish
    APPSPAWN,
};
enum class MemoryReduceDegree : uint8_t {
    LOW = 0,
    HIGH,
};

using HeapVisitor = const std::function<void(BaseObject*)>;

class PUBLIC_API BaseRuntime {
public:
    BaseRuntime() = default;
    ~BaseRuntime() = default;

    static BaseRuntime *GetInstance();
    static void DestroyInstance();

    void PreFork(ThreadHolder *holder);
    void PostFork();

    void Init(const RuntimeParam &param);   // Support setting custom parameters
    void Init();                            // Use default parameters
    void Fini();

    // Need refactor, move to other file
    static void WriteBarrier(void* obj, void* field, void* ref);
    static void* ReadBarrier(void* obj, void* field);
    static void* ReadBarrier(void* field);
    static void* AtomicReadBarrier(void* obj, void* field, std::memory_order order);
    static void RequestGC(GcType type);
    static void WaitForGCFinish();
    static bool ForEachObj(HeapVisitor& visitor, bool safe);
    static void NotifyNativeAllocation(size_t bytes);
    static void NotifyNativeFree(size_t bytes);
    static void NotifyNativeReset(size_t oldBytes, size_t newBytes);
    static size_t GetNotifiedNativeSize();
    static void ChangeGCParams(bool isBackground);
    static bool CheckAndTriggerHintGC(MemoryReduceDegree degree);

    HeapParam &GetHeapParam()
    {
        return param_.heapParam;
    }

    GCParam &GetGCParam()
    {
        return param_.gcParam;
    }

    MutatorManager &GetMutatorManager()
    {
        return *mutatorManager_;
    }

    ThreadHolderManager &GetThreadHolderManager()
    {
        return *threadHolderManager_;
    }

    HeapManager &GetHeapManager()
    {
        return *heapManager_;
    }

    BaseClassRoots &GetBaseClassRoots()
    {
        return *baseClassRoots_;
    }

    BaseStringTableInterface<BaseStringTableImpl> &GetStringTable()
    {
        return *stringTable_;
    }
private:
    RuntimeParam param_ {};

    HeapManager* heapManager_ = nullptr;
    MutatorManager* mutatorManager_ = nullptr;
    ThreadHolderManager* threadHolderManager_  = nullptr;
    BaseClassRoots* baseClassRoots_ = nullptr;
    BaseStringTableInterface<BaseStringTableImpl>* stringTable_ = nullptr;
    static std::mutex vmCreationLock_;
    static BaseRuntime *baseRuntimeInstance_;
    static bool initialized_;
};
}  // namespace common
#endif // COMMON_INTERFACES_BASE_RUNTIME_H
