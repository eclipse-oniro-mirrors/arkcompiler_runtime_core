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

#include "plugins/ets/runtime/interop_js/interop_context.h"
#include "plugins/ets/runtime/interop_js/tooling/internal_api.h"

namespace ark::ets::interop::js {

bool ForEachFrameInUnionStack(void (*callback)(const void *frame, bool isStaticFrame, void *data), void *data)
{
    auto *mutator = Mutator::GetCurrent();
    if (UNLIKELY(mutator == nullptr)) {
        return false;
    }
    auto *execCtx = EtsExecutionContext::GetCurrent();
    if (UNLIKELY(execCtx == nullptr)) {
        return false;
    }
    auto *ctx = interop::js::InteropCtx::Current(execCtx);
    if (UNLIKELY(ctx == nullptr)) {
        return false;
    }
    return ctx->GetOrCreateCallStack().ForEachInteropFrame(callback, data);
}

bool UnionStackIsEmpty(bool *isEmpty)
{
    auto *mutator = Mutator::GetCurrent();
    if (UNLIKELY(mutator == nullptr)) {
        return false;
    }
    auto *execCtx = EtsExecutionContext::GetCurrent();
    if (UNLIKELY(execCtx == nullptr)) {
        return false;
    }
    auto *ctx = interop::js::InteropCtx::Current(execCtx);
    if (UNLIKELY(ctx == nullptr)) {
        return false;
    }
    auto &callStack = ctx->GetOrCreateCallStack();
    *isEmpty = (!ctx->GetInteropHybridStackEnabled()) ||
               ((callStack.GetRecords().Size() == 0) && (callStack.GetStaticTopFrame() == nullptr));
    if (!*isEmpty) {
        *isEmpty = (callStack.GetDynamicTopFrameSP() == nullptr);
    }
    return true;
}

const void *GetEcmaVM()
{
    auto *thread = Mutator::GetCurrent();
    if (UNLIKELY(thread == nullptr)) {
        return nullptr;
    }
    auto *execCtx = EtsExecutionContext::GetCurrent();
    if (UNLIKELY(execCtx == nullptr)) {
        return nullptr;
    }
    auto *ctx = interop::js::InteropCtx::Current(execCtx);
    if (UNLIKELY(ctx == nullptr)) {
        return nullptr;
    }
    return ctx->GetOrCreateCallStack().GetEcmaVM();
}

bool IsHybridStackEnabled()
{
    auto *mutator = Mutator::GetCurrent();
    if (UNLIKELY(mutator == nullptr)) {
        return false;
    }
    auto *execCtx = EtsExecutionContext::GetCurrent();
    if (UNLIKELY(execCtx == nullptr)) {
        return false;
    }
    auto *ctx = interop::js::InteropCtx::Current(execCtx);
    if (UNLIKELY(ctx == nullptr)) {
        return false;
    }
    bool isEnabled = ctx->GetInteropHybridStackEnabled();
    if (isEnabled) {
        ctx->GetOrCreateCallStack();
    }
    return isEnabled;
}

bool GetStaticFrameInfo(const void *frame, arkplatform::HybridFrameInfo &frameInfo)
{
    auto *staticFrame = static_cast<const arkplatform::StaticFrameInfo *>(frame);
    if (staticFrame == nullptr || staticFrame->methodPtr == nullptr) {
        return false;
    }

    frameInfo = {};
    auto *method = static_cast<Method *>(staticFrame->methodPtr);
    auto methodAddr = reinterpret_cast<uintptr_t>(method);
    if (method == nullptr || (methodAddr & (alignof(Method) - 1)) != 0) {
        return false;
    }
    frameInfo.SetFunctionName(utf::Mutf8AsCString(reinterpret_cast<const uint8_t *>(method->GetName().data)));
    std::string_view classNameView(utf::Mutf8AsCString(method->GetClassName().data));
    constexpr size_t MIN_LEN = 2;
    if (classNameView.size() >= MIN_LEN && classNameView.front() == 'L' && classNameView.back() == ';') {
        classNameView = classNameView.substr(1, classNameView.size() - MIN_LEN);
    }
    std::array<char, arkplatform::HYBRID_FRAME_CLASS_NAME_SIZE> classNameBuf {};
    size_t classNameLen =
        (classNameView.size() < classNameBuf.size() - 1) ? classNameView.size() : (classNameBuf.size() - 1);
    for (size_t i = 0; i < classNameLen; ++i) {
        classNameBuf[i] = (classNameView[i] != '/') ? classNameView[i] : '.';
    }
    frameInfo.SetClassName(std::string_view(classNameBuf.data(), classNameLen));
    frameInfo.isStaticFrame = true;
    frameInfo.nativePtr = method;
    frameInfo.lineNumber = method->GetLineNumFromBytecodeOffset(staticFrame->bcOffset);
    frameInfo.SetUrl(utf::Mutf8AsCString(reinterpret_cast<const uint8_t *>(method->GetClassSourceFile().data)));

    return true;
}

bool GetDynamicFrameInfo(const void *frame, arkplatform::HybridFrameInfo &frameInfo)
{
    auto *ctx = InteropCtx::Current();
    if (ctx == nullptr) {
        return false;
    }
    auto *ecmaInterface = ctx->GetECMAInterface();
    if (ecmaInterface == nullptr) {
        return false;
    }
    return ecmaInterface->GetDynamicFrameInfo(frame, frameInfo);
}

bool DynamicTopFrameIsValid(void *signalContextPtr)
{
    auto *mutator = Mutator::GetCurrent();
    if (UNLIKELY(mutator == nullptr)) {
        return false;
    }
    auto *execCtx = EtsExecutionContext::GetCurrent();
    if (UNLIKELY(execCtx == nullptr)) {
        return false;
    }
    auto *ctx = interop::js::InteropCtx::Current(execCtx);
    if (UNLIKELY(ctx == nullptr)) {
        return false;
    }
    return ctx->GetOrCreateCallStack().DynamicTopFrameIsValid(signalContextPtr);
}

bool IsJSGCRunning()
{
    auto *ctx = InteropCtx::Current();
    if (ctx == nullptr) {
        return false;
    }
    auto *ecmaInterface = ctx->GetECMAInterface();
    if (ecmaInterface == nullptr) {
        return false;
    }
    return ecmaInterface->IsJSGCRunning();
}
}  // namespace ark::ets::interop::js
