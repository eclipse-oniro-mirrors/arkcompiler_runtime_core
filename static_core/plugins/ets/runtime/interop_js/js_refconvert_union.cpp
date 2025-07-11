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
#include "plugins/ets/runtime/interop_js/js_refconvert_union.h"

namespace ark::ets::interop::js {

napi_value JSRefConvertUnion::WrapImpl([[maybe_unused]] InteropCtx *ctx, [[maybe_unused]] EtsObject *obj)
{
    UNREACHABLE();
}

EtsObject *JSRefConvertUnion::UnwrapImpl(InteropCtx *ctx, napi_value jsVal)
{
    // Check if object has SharedReference
    ets_proxy::SharedReference *sharedRef = ctx->GetSharedRefStorage()->GetReference(ctx->GetJSEnv(), jsVal);
    if (LIKELY(sharedRef != nullptr)) {
        return sharedRef->GetEtsObject();
    }

    auto *objectConverter = ctx->GetEtsClassWrappersCache()->Lookup(EtsClass::FromRuntimeClass(ctx->GetObjectClass()));
    auto *ret = objectConverter->Unwrap(ctx, jsVal);
    if (!ret->IsInstanceOf(klass_)) {
        ThrowJSErrorNotAssignable(ctx->GetJSEnv(), ret->GetClass(), klass_);
        return nullptr;
    }
    return ret;
}

}  // namespace ark::ets::interop::js
