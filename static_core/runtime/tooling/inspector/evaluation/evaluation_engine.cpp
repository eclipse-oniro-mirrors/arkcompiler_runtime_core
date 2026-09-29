/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

#include "evaluation/evaluation_engine.h"

#include "include/class.h"
#include "include/tooling/pt_thread.h"

namespace ark::tooling::inspector {
Expected<std::pair<TypedValue, ObjectHeader *>, Error> PtThreadEvaluationEngine::EvaluateExpression(
    uint32_t frameNumber, const ExpressionWrapper &bytecode, Method **method)
{
    ASSERT(!IsEvaluating());
    ASSERT(method != nullptr);

    VRegValue result;
    std::optional<Error> err;

    evaluating_ = true;
    if (*method != nullptr) {
        err = debugger_->EvaluateExpression(PtThread(thread_), frameNumber, *method, &result);
    } else {
        err = debugger_->EvaluateExpression(PtThread(thread_), frameNumber, bytecode, method, &result);
    }
    evaluating_ = false;

    if (err) {
        return Unexpected(*err);
    }
    ASSERT(*method != nullptr);

    auto *exception = thread_->GetException();
    // Current implementation must clear any occurred exceptions
    if (exception != nullptr) {
        auto *excClass = exception->ClassAddr<Class>();
        LOG(ERROR, DEBUGGER) << "PtThreadEvaluationEngine::EvaluateExpression: exception occurred, class="
                             << (excClass != nullptr ? excClass->GetName() : "null-class");
        thread_->SetException(nullptr);
    }

    auto typedResult = result.ToTypedValue((*method)->GetReturnType().GetId());
    if (typedResult.IsReference()) {
        auto *obj = typedResult.GetAsReference();
        if (obj != nullptr) {
            LOG(ERROR, DEBUGGER) << "PtThreadEvaluationEngine::EvaluateExpression: result object class="
                                 << obj->ClassAddr<Class>()->GetName();
        }
    }
    return std::make_pair(typedResult, exception);
}
}  // namespace ark::tooling::inspector
