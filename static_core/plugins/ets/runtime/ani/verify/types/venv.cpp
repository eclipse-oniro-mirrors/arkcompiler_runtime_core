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

#include "plugins/ets/runtime/ani/verify/types/venv.h"

#include "plugins/ets/runtime/ets_coroutine.h"

namespace ark::ets::ani::verify {

/*static*/
VEnv *VEnv::GetCurrent()
{
    if (Thread::GetCurrent() == nullptr) {
        return nullptr;
    }
    EtsCoroutine *coro = EtsCoroutine::GetCurrent();
    if (coro == nullptr) {
        return nullptr;
    }
    ani_env *env = coro->GetEtsNapiEnv();
    return reinterpret_cast<VEnv *>(env);
}

void VEnv::CreateLocalScope()
{
    GetEnvANIVerifier()->CreateLocalScope();
}

std::optional<PandaString> VEnv::DestroyLocalScope()
{
    return GetEnvANIVerifier()->DestroyLocalScope();
}

void VEnv::CreateEscapeLocalScope()
{
    GetEnvANIVerifier()->CreateEscapeLocalScope();
}

std::optional<PandaString> VEnv::DestroyEscapeLocalScope(VRef *vref)
{
    return GetEnvANIVerifier()->DestroyEscapeLocalScope(vref);
}

void VEnv::DeleteLocalVerifiedRef(VRef *vref)
{
    GetEnvANIVerifier()->DeleteLocalVerifiedRef(vref);
}

VRef *VEnv::AddGloablVerifiedRef(ani_ref gref)
{
    return GetEnvANIVerifier()->AddGloablVerifiedRef(gref);
}

void VEnv::DeleteDeleteGlobalRef(VRef *vgref)
{
    GetEnvANIVerifier()->DeleteDeleteGlobalRef(vgref);
}

bool VEnv::IsValidGlobalVerifiedRef(VRef *vgref)
{
    return GetEnvANIVerifier()->IsValidGlobalVerifiedRef(vgref);
}

EnvANIVerifier *VEnv::GetEnvANIVerifier()
{
    return PandaEnv::FromAniEnv(GetEnv())->GetEnvANIVerifier();
}

}  // namespace ark::ets::ani::verify
