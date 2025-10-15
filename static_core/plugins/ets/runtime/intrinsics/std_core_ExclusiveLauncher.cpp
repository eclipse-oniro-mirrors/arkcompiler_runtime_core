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

#include "intrinsics.h"
#include "libarkbase/os/mutex.h"
#include "runtime/include/exceptions.h"
#include "runtime/include/thread.h"
#include "runtime/include/thread_scopes.h"
#include "runtime/mem/refstorage/reference.h"
#include "plugins/ets/runtime/ets_coroutine.h"
#include "plugins/ets/runtime/ets_handle.h"
#include "plugins/ets/runtime/ets_utils.h"
#include "plugins/ets/runtime/ets_vm.h"
#include "plugins/ets/runtime/types/ets_object.h"
#include "plugins/ets/runtime/types/ets_string.h"

#include <cstdint>
#include <thread>

namespace ark::ets::intrinsics {

static constexpr EtsInt INVALID_WORKER_ID = -1;

EtsObject *CreateMainWorker()
{
    auto *pandaVM = PandaEtsVM::GetCurrent();
    auto *classLinker = pandaVM->GetClassLinker();
    auto mutf8Name = reinterpret_cast<const uint8_t *>("Lstd/core/EAWorker;");
    auto *ext = classLinker->GetEtsClassLinkerExtension();
    auto *klass = ets::EtsClass::FromRuntimeClass(ext->GetClass(mutf8Name));
    if (klass == nullptr) {
        LOG(ERROR, COROUTINES) << "Load EAWorker failed";
        return nullptr;
    }
    return EtsObject::Create(EtsCoroutine::GetCurrent(), klass);
}

void SetCurrentWorkerPriority(int priority)
{
    QosHelper::SetCurrentWorkerPriority(static_cast<Priority>(priority));
}

static void RunExclusiveTask(mem::Reference *taskRef, mem::GlobalObjectStorage *refStorage)
{
    ScopedManagedCodeThread managedCode(EtsCoroutine::GetCurrent());
    auto *taskObj = EtsObject::FromCoreType(refStorage->Get(taskRef));
    refStorage->Remove(taskRef);
    LambdaUtils::InvokeVoid(EtsCoroutine::GetCurrent(), taskObj);
}

Coroutine *TryCreateEACoroutine(PandaEtsVM *etsVM, bool needInterop, bool &limitIsReached, bool &jsEnvEmpty)
{
    auto *runtime = Runtime::GetCurrent();
    auto *coroMan = etsVM->GetCoroutineManager();
    auto *ifaceTable = EtsCoroutine::CastFromThread(coroMan->GetMainThread())->GetExternalIfaceTable();

    auto *exclusiveCoro = coroMan->CreateExclusiveWorkerForThread(runtime, etsVM);
    // exclusiveCoro == nullptr means that we reached the limit of eaworkers count or memory resources
    if (exclusiveCoro == nullptr) {
        limitIsReached = true;
        LOG(ERROR, COROUTINES) << "The limit of Exclusive Workers has been reached";
        return nullptr;
    }

    // early return to avoid waste time on creating jsEnv
    if (!needInterop) {
        return exclusiveCoro;
    }

    auto *jsEnv = ifaceTable->CreateJSRuntime();
    // current we cannot create JSVM instance without jsEnv
    // so we cannot create eaworker support interop withoutJSEnv
    if (jsEnv == nullptr) {
        jsEnvEmpty = true;
        LOG(ERROR, COROUTINES) << "Cannot create EAWorker support interop without JsEnv";
        return nullptr;
    }

    ifaceTable->CreateInteropCtx(exclusiveCoro, jsEnv);
    return exclusiveCoro;
}

static constexpr uint64_t ASYNC_WORK_WAITING_TIME = 10;

void RunTaskOnEACoroutine(PandaEtsVM *etsVM, bool needInterop, mem::Reference *taskRef)
{
    RunExclusiveTask(taskRef, etsVM->GetGlobalObjectStorage());
    if (needInterop) {
        auto *w = Coroutine::GetCurrent()->GetWorker();
        while (w->IsExternalSchedulingEnabled()) {
            w->ProcessAsyncWork();
            auto *coroMan = Coroutine::GetCurrent()->GetManager();
            TimerEvent timerEvt(coroMan, 0);
            timerEvt.Lock();
            timerEvt.SetExpirationTime(coroMan->GetCurrentTime() + ASYNC_WORK_WAITING_TIME);
            coroMan->Await(&timerEvt);
        }
    }
}

bool HasPendingError(bool limitIsReached, bool jsEnvEmpty)
{
    if (limitIsReached) {
        ThrowCoroutinesLimitExceedError("The limit of Exclusive Workers has been reached");
        return true;
    }
    if (jsEnvEmpty) {
        ThrowRuntimeException("Cannot create EAWorker support interop without JsEnv");
        return true;
    }
    return false;
}

EtsInt ExclusiveLaunch(EtsObject *task, uint8_t needInterop, EtsString *name)
{
    auto *coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);
    auto *etsVM = coro->GetPandaVM();
    if (etsVM->GetCoroutineManager()->IsExclusiveWorkersLimitReached()) {
        ThrowCoroutinesLimitExceedError("The limit of Exclusive Workers has been reached");
        return INVALID_WORKER_ID;
    }
    auto *refStorage = etsVM->GetGlobalObjectStorage();
    auto *taskRef = refStorage->Add(task->GetCoreType(), mem::Reference::ObjectType::GLOBAL);
    ASSERT(taskRef != nullptr);
    auto limitIsReached = false;
    auto jsEnvEmpty = false;
    bool supportInterop = static_cast<bool>(needInterop);
    auto *interopCtx = coro->GetWorker()->GetLocalStorage().Get<CoroutineWorker::DataIdx::INTEROP_CTX_PTR, void *>();
    int32_t workerId = 0;

    if (supportInterop && interopCtx == nullptr) {
        ThrowRuntimeException("Cannot create EAWorker support interop without JsEnv");
        return INVALID_WORKER_ID;
    }
    {
        PandaVector<uint8_t> nameBuf;
        EtsHandleScope handleScope(coro);
        EtsHandle<EtsString> nameHandle(coro, name);
        PandaString nameStr(nameHandle->ConvertToStringView(&nameBuf));
        ScopedNativeCodeThread nativeScope(coro);
        auto event = os::memory::Event();
        auto t = std::thread([&jsEnvEmpty, &limitIsReached, &event, &workerId, etsVM, taskRef, supportInterop]() {
            auto *eaCoro = TryCreateEACoroutine(etsVM, supportInterop, limitIsReached, jsEnvEmpty);
            if (eaCoro == nullptr) {
                LOG(ERROR, COROUTINES) << "Cannot create EAWorker";
                event.Fire();
                return;
            }
            workerId = eaCoro->GetWorker()->GetId();
            event.Fire();
            RunTaskOnEACoroutine(etsVM, supportInterop, taskRef);
            etsVM->GetCoroutineManager()->DestroyExclusiveWorker();
        });
        os::thread::SetThreadName(t.native_handle(), nameStr.c_str());
        event.Wait();
        t.detach();
    }
    if (HasPendingError(limitIsReached, jsEnvEmpty)) {
        return INVALID_WORKER_ID;
    }
    return workerId;
}

void JoinExclusiveWorker(EtsInt workerId, EtsObject *finalTask)
{
    auto *coro = Coroutine::GetCurrent();
    auto *coroMan = coro->GetManager();
    auto *taskRef =
        coro->GetVM()->GetGlobalObjectStorage()->Add(finalTask->GetCoreType(), mem::Reference::ObjectType::GLOBAL);
    auto eaGroup = CoroutineWorkerGroup::GenerateExactWorkerId(workerId);
    auto joiningFunc = [](void *param) {
        auto *ref = static_cast<mem::Reference *>(param);
        auto *eaCoro = EtsCoroutine::GetCurrent();
        RunExclusiveTask(ref, eaCoro->GetVM()->GetGlobalObjectStorage());
        eaCoro->GetWorker()->DestroyCallbackPoster();
    };
    coroMan->LaunchNative(joiningFunc, taskRef, "joining ea-coro", eaGroup, CoroutinePriority::DEFAULT_PRIORITY, true);
}

}  // namespace ark::ets::intrinsics
