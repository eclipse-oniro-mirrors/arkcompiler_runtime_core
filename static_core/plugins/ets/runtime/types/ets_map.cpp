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
#include "intrinsics/helpers/ets_intrinsics_helpers.h"
#include "plugins/ets/runtime/types/ets_bigint.h"

namespace ark::ets {
static constexpr int NUMERIC_INT_MAX = std::numeric_limits<int>::max();
static constexpr int NUMERIC_INT_MIN = std::numeric_limits<int>::min();

[[maybe_unused]] static bool Compare(EtsCoroutine *coro, EtsObject *a, EtsObject *b)
{
    return ark::ets::intrinsics::helpers::SameValueZero(coro, a, b);
}

template <typename T>
static uint32_t GetHashFloats(EtsObject *obj)
{
    T val = EtsBoxPrimitive<T>::Unbox(obj);
    // Like ETS.
    if (std::isnan(val)) {
        return 0;
    }
    if (val > static_cast<T>(NUMERIC_INT_MAX)) {
        return NUMERIC_INT_MAX;
    }

    if (val < static_cast<T>(NUMERIC_INT_MIN)) {
        return NUMERIC_INT_MIN;
    }

    // AARCH64 needs float->int->uint cast to avoid returning 0.
    return static_cast<uint32_t>(static_cast<int32_t>(val));
}

uint32_t EtsEscompatMap::GetHashCode(EtsObject *&key)
{
    auto *klass = key->GetClass();
    if (klass->IsNullValue()) {
        return 0;
    }

    if (klass->IsBoxed()) {
        auto type = klass->GetBoxedType();
        switch (type) {
            case EtsClass::BoxedType::INT:
                return EtsBoxPrimitive<EtsInt>::Unbox(key);
            case EtsClass::BoxedType::DOUBLE:
                return GetHashFloats<EtsDouble>(key);
            case EtsClass::BoxedType::FLOAT:
                return GetHashFloats<EtsFloat>(key);
            case EtsClass::BoxedType::LONG:
                return EtsBoxPrimitive<EtsLong>::Unbox(key);
            case EtsClass::BoxedType::SHORT:
                return EtsBoxPrimitive<EtsShort>::Unbox(key);
            case EtsClass::BoxedType::BYTE:
                return EtsBoxPrimitive<EtsByte>::Unbox(key);
            case EtsClass::BoxedType::CHAR:
                return EtsBoxPrimitive<EtsChar>::Unbox(key);
            case EtsClass::BoxedType::BOOLEAN:
                return EtsBoxPrimitive<EtsBoolean>::Unbox(key);
            default:
                LOG(FATAL, RUNTIME) << "Unknown boxed type";
                UNREACHABLE();
        }
    }

    if (klass->IsStringClass()) {
        return EtsString::FromEtsObject(key)->GetCoreType()->GetHashcode();
    }

    if (klass->IsBigInt()) {
        return (reinterpret_cast<EtsBigInt *>(key))->GetHashCode();
    }

    return key->GetHashCode();
}

}  // namespace ark::ets
