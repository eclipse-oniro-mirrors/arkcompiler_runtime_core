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

#include "plugins/ets/runtime/interop_js/interop_stacks.h"
#include "plugins/ets/runtime/interop_js/interop_context.h"
#include "runtime/include/stack_walker.h"
#include "runtime/interpreter/frame.h"
#include "runtime/include/method.h"

namespace ark::ets::interop::js {

InteropCallStack::InteropCallStack()
{
    void *pool = ark::os::mem::MapRWAnonymousWithAlignmentRaw(POOL_SIZE, ark::os::mem::GetPageSize());
    if (pool == nullptr) {
        InteropFatal("InteropCallStack alloc failed");
    }
    startAddr_ = reinterpret_cast<Record *>(pool);
    endAddr_ = reinterpret_cast<Record *>(ToUintPtr(pool) + POOL_SIZE);
    curAddr_ = startAddr_;
}

void *InteropCallStack::GetStaticTopFrame() const
{
    auto *coro = EtsCoroutine::GetCurrent();
    if (UNLIKELY(coro == nullptr)) {
        return nullptr;
    }
    auto stack = StackWalker::Create(coro);
    if (!stack.HasFrame()) {
        return nullptr;
    }
    // NOLINTNEXTLINE
    return stack.IsCFrame() ? static_cast<void *>(&stack.GetCFrame()) : static_cast<void *>(stack.GetIFrame());
}

void *InteropCallStack::GetDynamicTopFrameSP() const
{
    auto *ctx = interop::js::InteropCtx::Current();
    if (UNLIKELY(ctx == nullptr)) {
        return nullptr;
    }
    auto *ecmaInterface = ctx->GetECMAInterface();
    if (UNLIKELY(ecmaInterface == nullptr)) {
        return nullptr;
    }
    return ecmaInterface->GetTopFrameSPFromDynamic();
}

bool InteropCallStack::ForEachDynamicFrame(void *currFrameSP, void *toFrameSP,
                                           void (*cb)(const void *frame, void *data), void *data) const
{
    if (UNLIKELY(currFrameSP == nullptr)) {
        return false;
    }
    auto *ctx = interop::js::InteropCtx::Current();
    if (UNLIKELY(ctx == nullptr)) {
        return false;
    }
    auto *ecmaInterface = ctx->GetECMAInterface();
    if (UNLIKELY(ecmaInterface == nullptr)) {
        return false;
    }
    return ecmaInterface->ForEachDynamicFrame(currFrameSP, toFrameSP, cb, data);
}

bool InteropCallStack::ForEachStaticFrame(StackWalker *stack, void *toFrame, void (*cb)(const void *frame, void *data),
                                          void *data) const
{
    for (; stack->HasFrame(); stack->NextFrame()) {
        void *currFrame =
            stack->IsCFrame() ? static_cast<void *>(&stack->GetCFrame()) : static_cast<void *>(stack->GetIFrame());
        if (currFrame == toFrame) {
            break;
        }
        Method *method = stack->GetMethod();
        if (method == nullptr) {
            continue;
        }
        Frame *frame {};
        if (!stack->IsCFrame()) {
            frame = stack->GetIFrame();
        }

        arkplatform::StaticFrameInfo staticFrame {method, frame != nullptr ? frame->GetBytecodeOffset() : 0, frame};
        cb(&staticFrame, data);
    }
    return true;
}

bool InteropCallStack::ForEachInteropFrame(void (*callback)(const void *frame, bool isStaticFrame, void *data),
                                           void *data)
{
    if (UNLIKELY(!InteropCtx::IsHybridStackEnabled())) {
        return false;
    }
    auto *execCtx = JobExecutionContext::GetCurrent();
    if (UNLIKELY(execCtx == nullptr)) {
        return false;
    }

    // Trampoline helpers: invoke the outer (frame, isStaticFrame, data) callback
    // from the inner (frame, data) callback, injecting the known isStaticFrame value.
    struct TrampolineData {
        void (*callback)(const void *frame, bool isStaticFrame, void *data);
        void *data;
    };

    auto staticTrampoline = [](const void *frame, void *userData) {
        auto *td = static_cast<TrampolineData *>(userData);
        td->callback(frame, true, td->data);
    };

    auto dynamicTrampoline = [](const void *frame, void *userData) {
        auto *td = static_cast<TrampolineData *>(userData);
        td->callback(frame, false, td->data);
    };

    // Get the current top stack and traverse the stack between the current node and the recorded node.
    StackWalker staticStack = StackWalker::Create(execCtx);
    void *dynamicFrameSP = GetDynamicTopFrameSP();

    auto records = GetRecords();
    int frameSize = static_cast<int>(records.size());
    TrampolineData td {callback, data};

    for (int fIdx = frameSize - 1; fIdx >= 0; fIdx--) {
        if (!ForEachDynamicFrame(dynamicFrameSP, records[fIdx].dynamicSP, dynamicTrampoline, &td)) {
            return false;
        }
        dynamicFrameSP = records[fIdx].dynamicSP;
        if (!ForEachStaticFrame(&staticStack, records[fIdx].staticEntryFrame, staticTrampoline, &td)) {
            return false;
        }
    }

    if (dynamicFrameSP != nullptr) {
        if (!ForEachDynamicFrame(dynamicFrameSP, nullptr, dynamicTrampoline, &td)) {
            return false;
        }
    }
    return ForEachStaticFrame(&staticStack, nullptr, staticTrampoline, &td);
}

const void *InteropCallStack::GetEcmaVM() const
{
    auto *ctx = interop::js::InteropCtx::Current();
    if (UNLIKELY(ctx == nullptr)) {
        return nullptr;
    }
    auto *ecmaInterface = ctx->GetECMAInterface();
    if (UNLIKELY(ecmaInterface == nullptr)) {
        return nullptr;
    }
    return ecmaInterface->GetEcmaVM();
}

bool InteropCallStack::DynamicTopFrameIsValid(void *signalContextPtr)
{
    auto *ctx = interop::js::InteropCtx::Current();
    if (UNLIKELY(ctx == nullptr)) {
        return false;
    }
    auto *ecmaInterface = ctx->GetECMAInterface();
    if (UNLIKELY(ecmaInterface == nullptr)) {
        return false;
    }
    return ecmaInterface->DynamicTopFrameIsValid(signalContextPtr);
}

}  // namespace ark::ets::interop::js
