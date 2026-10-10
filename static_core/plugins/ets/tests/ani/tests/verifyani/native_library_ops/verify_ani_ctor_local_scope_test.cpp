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

#include <ani.h>
#include <array>

constexpr ani_int TEST_VALUE = 42;

static ani_int Value([[maybe_unused]] ani_env *env, [[maybe_unused]] ani_object object)
{
    return TEST_VALUE;
}

ANI_EXPORT ani_status ANI_Constructor(ani_vm *vm, uint32_t *result)
{
    ani_env *env {};
    if (vm->GetEnv(ANI_VERSION_1, &env) != ANI_OK) {
        return ANI_ERROR;
    }

    ani_class cls {};
    if (env->FindClass("verify_native_library_constructor_test.VerifyAniCtorLocalScopeClass", &cls) != ANI_OK) {
        return ANI_ERROR;
    }

    std::array methods = {
        ani_native_function {"value", ":i", reinterpret_cast<void *>(Value)},
    };
    if (env->Class_BindNativeMethods(cls, methods.data(), methods.size()) != ANI_OK) {
        return ANI_ERROR;
    }

    *result = ANI_VERSION_1;
    return ANI_OK;
}
