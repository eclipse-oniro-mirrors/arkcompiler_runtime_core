/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#include "ets_handle.h"
#include "include/mem/panda_containers.h"
#include "runtime/runtime_helpers.h"
#include "types/ets_method.h"
#include "types/ets_stacktrace_element.h"
#include "plugins/ets/runtime/ets_platform_types.h"
#include "plugins/ets/runtime/ets_exceptions.h"
#include "plugins/ets/runtime/ets_panda_file_items.h"
#ifdef PANDA_ETS_INTEROP_JS
#include "plugins/ets/runtime/interop_js/interop_context.h"
#include "plugins/ets/runtime/interop_js/tooling/internal_api.h"
#include "hybrid/hybrid_frame_info.h"
#endif /* PANDA_ETS_INTEROP_JS */

#include "runtime/include/stack_walker.h"
#include "runtime/interpreter/runtime_interface.h"
#include "runtime/handle_scope.h"
#include "runtime/handle_scope-inl.h"

#include "plugins/ets/runtime/intrinsics/helpers/ets_intrinsics_helpers.h"

namespace ark::ets::intrinsics {

EtsStackTraceElement *CreateStackTraceElement(StackWalker *stack)
{
    auto executionCtx = EtsExecutionContext::GetCurrent();
    [[maybe_unused]] EtsHandleScope scope(executionCtx);

    helpers::StackTraceElementInfo info;
    if (!GetStackTraceElementBasicInfo(stack, &info)) {
        ASSERT(executionCtx->GetMT()->HasPendingException());
        return nullptr;
    }

    EtsHandle<EtsString> classNameHandle(executionCtx, EtsString::CreateFromMUtf8(info.className.c_str()));
    if (UNLIKELY(classNameHandle.GetPtr() == nullptr)) {
        ASSERT(executionCtx->GetMT()->HasPendingException());
        return nullptr;
    }
    EtsHandle<EtsString> methodNameHandle(executionCtx, EtsString::CreateFromMUtf8(info.methodName.c_str()));
    if (UNLIKELY(methodNameHandle.GetPtr() == nullptr)) {
        ASSERT(executionCtx->GetMT()->HasPendingException());
        return nullptr;
    }

    auto *stackTraceElement = EtsStackTraceElement::Create(executionCtx);
    if (UNLIKELY(stackTraceElement == nullptr)) {
        ASSERT(executionCtx->GetMT()->HasPendingException());
        return nullptr;
    }
    auto element = EtsHandle<EtsStackTraceElement>(executionCtx, stackTraceElement);
    element->SetClassName(classNameHandle.GetPtr());
    element->SetMethodName(methodNameHandle.GetPtr());

    EtsString *sourceFileName = EtsString::CreateFromMUtf8(info.sourceFile);
    if (UNLIKELY(sourceFileName == nullptr)) {
        ASSERT(executionCtx->GetMT()->HasPendingException());
        return nullptr;
    }
    element->SetSourceFileName(sourceFileName);
    element->SetLineNumber(info.lineNumber);
    element->SetIsStaticFrame(static_cast<EtsBoolean>(true));
    return element.GetPtr();
}

#ifdef PANDA_ETS_INTEROP_JS
void CreateDynamicStaticStackTraceElement(PandaVector<EtsHandle<EtsStackTraceElement>> &stackTraceElements)
{
    auto executionCtx = EtsExecutionContext::GetCurrent();

    auto *ctx = interop::js::InteropCtx::Current(executionCtx);
    if (UNLIKELY(ctx == nullptr)) {
        ASSERT(executionCtx->GetMT()->HasPendingException());
        return;
    }

    auto *ecmaInterface = ctx->GetECMAInterface();
    if (UNLIKELY(ecmaInterface == nullptr)) {
        ASSERT(executionCtx->GetMT()->HasPendingException());
        return;
    }

    ctx->GetOrCreateCallStack().ForEachInteropFrame(
        [executionCtx, ecmaInterface, &stackTraceElements](const void *frame, bool isStaticFrame) {
            arkplatform::HybridFrameInfo info;
            bool success = false;

            if (isStaticFrame) {
                success = ark::ets::interop::js::GetStaticFrameInfo(frame, info);
                if (!success) {
                    LOG(ERROR, STDLIB) << "GetStaticFrameInfo failed: method from stack is nullptr.";
                    return;
                }
            } else {
                success = ecmaInterface->GetSourceMappedFrameInfo(frame, info);
                if (!success) {
                    LOG(ERROR, STDLIB) << "GetSourceMappedFrameInfo failed: method from stack is nullptr.";
                    return;
                }
            }

            auto *stackTraceElement = EtsStackTraceElement::Create(executionCtx);
            if (UNLIKELY(stackTraceElement == nullptr)) {
                ASSERT(executionCtx->GetMT()->HasPendingException());
                return;
            }
            auto element = EtsHandle<EtsStackTraceElement>(executionCtx, stackTraceElement);
            EtsHandle<EtsString> className(executionCtx, EtsString::CreateFromMUtf8(info.GetClassName().data()));
            if (UNLIKELY(className.GetPtr() == nullptr)) {
                ASSERT(executionCtx->GetMT()->HasPendingException());
                return;
            }
            EtsHandle<EtsString> functionName(executionCtx, EtsString::CreateFromMUtf8(info.GetFunctionName().data()));
            if (UNLIKELY(functionName.GetPtr() == nullptr)) {
                ASSERT(executionCtx->GetMT()->HasPendingException());
                return;
            };
            EtsString *sourceFileName =
                EtsString::CreateFromMUtf8(info.GetUrl().empty() ? "<unknown>" : info.GetUrl().data());
            if (UNLIKELY(sourceFileName == nullptr)) {
                ASSERT(executionCtx->GetMT()->HasPendingException());
                return;
            }
            element->SetClassName(className.GetPtr());
            element->SetMethodName(functionName.GetPtr());
            element->SetSourceFileName(sourceFileName);
            element->SetLineNumber(info.lineNumber);
            element->SetColNumber(isStaticFrame ? 0 : info.columnNumber);
            element->SetIsStaticFrame(static_cast<EtsBoolean>(isStaticFrame));
            stackTraceElements.push_back(element);
        });
}
#endif /* PANDA_ETS_INTEROP_JS */

extern "C" EtsObjectArray *ArkRuntimeStackTraceProvisionStackTrace()
{
    auto executionCtx = EtsExecutionContext::GetCurrent();
    [[maybe_unused]] EtsHandleScope scope(executionCtx);

    auto stackTraceElementClass = PlatformTypes(executionCtx)->arkruntimeStackTraceElement;
    EtsHandle<EtsObjectArray> requesterStackTrace(executionCtx, nullptr);
    if (auto *stackTraceRef = executionCtx->GetExclusiveScopeStackTraceRef(); stackTraceRef != nullptr) {
        requesterStackTrace = EtsHandle<EtsObjectArray>(
            executionCtx,
            EtsObjectArray::FromCoreType(executionCtx->GetPandaVM()->GetGlobalObjectStorage()->Get(stackTraceRef)));
    }

    PandaVector<EtsHandle<EtsStackTraceElement>> stackTraceElements;
#ifdef PANDA_ETS_INTEROP_JS
    bool isEmpty = true;
    interop::js::UnionStackIsEmpty(&isEmpty);
    if (!isEmpty) {
        CreateDynamicStaticStackTraceElement(stackTraceElements);
    } else {
        for (auto stack = StackWalker::Create(executionCtx->GetMT()); stack.HasFrame(); stack.NextFrame()) {
            auto element = EtsHandle<EtsStackTraceElement>(executionCtx, CreateStackTraceElement(&stack));
            if (UNLIKELY(element.GetPtr() == nullptr)) {
                ASSERT(executionCtx->GetMT()->HasPendingException());
                return nullptr;
            }
            stackTraceElements.push_back(element);
        }
    }
#else
    for (auto stack = StackWalker::Create(executionCtx->GetMT()); stack.HasFrame(); stack.NextFrame()) {
        auto element = EtsHandle<EtsStackTraceElement>(executionCtx, CreateStackTraceElement(&stack));
        if (UNLIKELY(element.GetPtr() == nullptr)) {
            ASSERT(executionCtx->GetMT()->HasPendingException());
            return nullptr;
        }
        stackTraceElements.push_back(element);
    }
#endif /* PANDA_ETS_INTEROP_JS */

    auto linesSize = static_cast<uint32_t>(stackTraceElements.size());

    uint32_t requesterLinesSize =
        requesterStackTrace.GetPtr() != nullptr ? static_cast<uint32_t>(requesterStackTrace->GetLength()) : 0U;

    auto *resultArray = EtsObjectArray::Create(stackTraceElementClass, linesSize + requesterLinesSize);
    if (UNLIKELY(resultArray == nullptr)) {
        ASSERT(executionCtx->GetMT()->HasPendingException());
        return nullptr;
    }
    EtsHandle<EtsObjectArray> resultArrayHandle(executionCtx, resultArray);
    uint32_t i = 0;
    for (; i < linesSize; i++) {
        resultArrayHandle.GetPtr()->Set(i, stackTraceElements[i]->AsObject());
    }
    for (uint32_t requesterIndex = 0U; requesterIndex < requesterLinesSize; ++requesterIndex) {
        resultArrayHandle.GetPtr()->Set(i, requesterStackTrace->Get(requesterIndex));
        i++;
    }
    return resultArrayHandle.GetPtr();
}

}  // namespace ark::ets::intrinsics
