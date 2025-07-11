/*
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

#ifndef PANDA_PRODUCT_BUILD

#include "plugins/ets/tests/checked/jitinterface/compile_method.h"

#include "plugins/ets/runtime/ets_class_linker_extension.h"
#include "plugins/ets/runtime/ets_stubs-inl.h"
#include "plugins/ets/runtime/ani/scoped_objects_fix.h"
#include "runtime/compiler.h"

namespace ark::ets::ani {

ani_int CompileMethod(ani_env *env, ani_string name)
{
    if (!Runtime::GetCurrent()->IsJitEnabled()) {
        return 1;
    }
    ScopedManagedCodeFix s(env);
    auto str = s.ToInternalType(name);
    auto *caller = GetMethodOwnerClassInFrames(s.GetCoroutine(), 0);
    ClassLinkerContext *ctx = nullptr;
    if (caller != nullptr) {
        ctx = caller->GetLoadContext();
    } else {
        ctx = PandaEtsVM::GetCurrent()->GetEtsClassLinkerExtension()->GetBootContext();
    }
    return ark::CompileMethodImpl(str->GetCoreType(), ctx);
}

}  // namespace ark::ets::ani

#endif  // PANDA_PRODUCT_BUILD
