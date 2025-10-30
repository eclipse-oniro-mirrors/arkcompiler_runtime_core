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

#include "plugins/ets/runtime/types/ets_map.h"
#include "plugins/ets/runtime/types/ets_bigint.h"
#include "plugins/ets/runtime/types/ets_object.h"
#include "plugins/ets/runtime/types/ets_string.h"

namespace ark::ets::intrinsics {

extern "C" EtsLong GetHashCodeString(EtsObject *obj)
{
    ASSERT(obj->GetClass()->IsStringClass());
    return static_cast<EtsInt>(EtsString::FromEtsObject(obj)->GetCoreType()->GetHashcode());
}

extern "C" EtsLong GetHashCodeBigint(EtsObject *obj)
{
    ASSERT(obj->GetClass()->IsBigInt());
    return static_cast<EtsInt>(reinterpret_cast<EtsBigInt *>(obj)->GetHashCode());
}

extern "C" EtsLong GetHashCodeObject(EtsObject *obj)
{
    return static_cast<EtsInt>(obj->GetHashCode());
}

}  // namespace ark::ets::intrinsics
