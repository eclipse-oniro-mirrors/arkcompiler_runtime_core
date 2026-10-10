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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_TOOLING_HELPERS_H
#define PANDA_PLUGINS_ETS_RUNTIME_TOOLING_HELPERS_H

#include <string_view>

#include "plugins/ets/runtime/ets_coroutine.h"
#include "plugins/ets/runtime/ets_execution_context.h"
#include "plugins/ets/runtime/types/ets_box_primitive.h"
#include "plugins/ets/runtime/types/ets_object.h"
#include "plugins/ets/runtime/types/ets_primitives.h"
#include "runtime/include/coretypes/tagged_value.h"
#include "runtime/include/tooling/pt_thread.h"
#include "runtime/include/tooling/vreg_value.h"
#include "libarkbase/utils/bit_utils.h"
#include <libarkfile/include/type.h>
#include "types/ets_primitives.h"

namespace ark::ets::tooling {

inline bool IsNullOrNullValue(EtsObject *obj)
{
    if (obj == nullptr) {
        return true;
    }
    auto *runtimeClass = obj->GetCoreType()->ClassAddr<Class>();
    return EtsClass::FromRuntimeClass(runtimeClass)->IsNullValue();
}

template <typename T>
struct EtsTypeName {
};

// CC-OFFNXT(G.PRE.02) code generation
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ETS_TYPE_NAME(TYPE_NAME)                             \
    template <>                                              \
    struct EtsTypeName<TYPE_NAME> {                          \
        static constexpr std::string_view NAME = #TYPE_NAME; \
    }

ETS_TYPE_NAME(EtsBoolean);
ETS_TYPE_NAME(EtsByte);
ETS_TYPE_NAME(EtsShort);
ETS_TYPE_NAME(EtsChar);
ETS_TYPE_NAME(EtsInt);
ETS_TYPE_NAME(EtsFloat);
ETS_TYPE_NAME(EtsDouble);
ETS_TYPE_NAME(EtsLong);

#undef ETS_TYPE_NAME

template <>
struct EtsTypeName<ObjectHeader *> {
    static constexpr std::string_view NAME = "EtsObject";
};

template <typename T, typename std::enable_if_t<std::is_same_v<EtsBoolean, T> || std::is_same_v<EtsByte, T> ||
                                                    std::is_same_v<EtsShort, T> || std::is_same_v<EtsChar, T> ||
                                                    std::is_same_v<EtsInt, T> || std::is_same_v<EtsLong, T>,
                                                int> = 0>
constexpr T VRegValueToEtsValue(ark::tooling::VRegValue value)
{
    return static_cast<T>(value.GetValue());
}

template <typename T, typename std::enable_if_t<std::is_same_v<EtsFloat, T>, int> = 0>
constexpr EtsFloat VRegValueToEtsValue(ark::tooling::VRegValue value)
{
    size_t bitsCount = sizeof(EtsFloat) * 8U;
    return bit_cast<EtsFloat>(
        static_cast<int32_t>(ExtractBits(static_cast<uint64_t>(value.GetValue()), 0U, bitsCount)));
}

template <typename T, typename std::enable_if_t<std::is_same_v<EtsDouble, T>, int> = 0>
constexpr EtsDouble VRegValueToEtsValue(ark::tooling::VRegValue value)
{
    return bit_cast<EtsDouble>(value.GetValue());
}

template <typename T, typename std::enable_if_t<std::is_same_v<ObjectHeader *, T>, int> = 0>
constexpr ObjectHeader *VRegValueToEtsValue(ark::tooling::VRegValue value)
{
    return reinterpret_cast<ObjectHeader *>(value.GetValue());
}

template <typename T, typename std::enable_if_t<std::is_same_v<EtsBoolean, T> || std::is_same_v<EtsByte, T> ||
                                                    std::is_same_v<EtsShort, T> || std::is_same_v<EtsChar, T> ||
                                                    std::is_same_v<EtsInt, T> || std::is_same_v<EtsLong, T>,
                                                int> = 0>
constexpr ark::tooling::VRegValue EtsValueToVRegValue(T value)
{
    return ark::tooling::VRegValue(value);
}

template <typename T, typename std::enable_if_t<std::is_same_v<EtsFloat, T>, int> = 0>
constexpr ark::tooling::VRegValue EtsValueToVRegValue(EtsFloat value)
{
    return ark::tooling::VRegValue(bit_cast<int32_t>(value));
}

template <typename T, typename std::enable_if_t<std::is_same_v<EtsDouble, T>, int> = 0>
constexpr ark::tooling::VRegValue EtsValueToVRegValue(EtsDouble value)
{
    return ark::tooling::VRegValue(bit_cast<int64_t>(value));
}

template <typename T, typename std::enable_if_t<std::is_same_v<ObjectHeader *, T>, int> = 0>
inline ark::tooling::VRegValue EtsValueToVRegValue(ObjectHeader *value)
{
    if (value == nullptr || IsNullOrNullValue(EtsObject::FromCoreType(value))) {
        return ark::tooling::VRegValue(0);
    }
    return ark::tooling::VRegValue(reinterpret_cast<int64_t>(value));
}

inline ark::tooling::PtThread CoroutineToPtThread(ManagedThread *mThread)
{
    return ark::tooling::PtThread(mThread);
}

inline bool IsRefSignature(std::string_view typeSignature)
{
    return typeSignature.empty() || typeSignature[0] == 'L' || typeSignature[0] == '[' || typeSignature[0] == '{';
}

inline bool IsTaggedSignature(std::string_view typeSignature)
{
    return !typeSignature.empty() && typeSignature[0] == 'A';
}

// Converts a raw tagged VRegValue into an EtsObject suitable for the debugger.
// Tagged values can hold ints, doubles, booleans, null/undefined, or heap
// object pointers — reinterpreting them unconditionally as ObjectHeader *
// would crash, so decode via coretypes::TaggedValue.
// Defined in helpers.cpp to keep the inline surface small.
EtsObject *TaggedVRegValueToEtsObject(ark::tooling::VRegValue vregValue, EtsExecutionContext *executionCtx);

// Converts an EtsObject back into a raw tagged VRegValue for writing to a
// tagged (signature 'A') vreg.
inline ark::tooling::VRegValue EtsObjectToTaggedVRegValue(EtsObject *obj)
{
    if (obj == nullptr || IsNullOrNullValue(obj)) {
        return ark::tooling::VRegValue(static_cast<int64_t>(coretypes::TaggedValue::Null().GetRawData()));
    }
    return ark::tooling::VRegValue(reinterpret_cast<int64_t>(obj->GetCoreType()));
}

template <typename T>
inline ark::tooling::VRegValue UnboxPrimitiveToVReg(EtsObject *obj);

template <>
inline ark::tooling::VRegValue UnboxPrimitiveToVReg<EtsBoolean>(EtsObject *obj)
{
    return ark::tooling::VRegValue(static_cast<int64_t>(EtsBoxPrimitive<EtsBoolean>::Unbox(obj)));
}

template <>
inline ark::tooling::VRegValue UnboxPrimitiveToVReg<EtsByte>(EtsObject *obj)
{
    return ark::tooling::VRegValue(static_cast<int64_t>(EtsBoxPrimitive<EtsByte>::Unbox(obj)));
}

template <>
inline ark::tooling::VRegValue UnboxPrimitiveToVReg<EtsShort>(EtsObject *obj)
{
    return ark::tooling::VRegValue(static_cast<int64_t>(EtsBoxPrimitive<EtsShort>::Unbox(obj)));
}

template <>
inline ark::tooling::VRegValue UnboxPrimitiveToVReg<EtsChar>(EtsObject *obj)
{
    return ark::tooling::VRegValue(static_cast<int64_t>(EtsBoxPrimitive<EtsChar>::Unbox(obj)));
}

template <>
inline ark::tooling::VRegValue UnboxPrimitiveToVReg<EtsInt>(EtsObject *obj)
{
    return ark::tooling::VRegValue(static_cast<int64_t>(EtsBoxPrimitive<EtsInt>::Unbox(obj)));
}

template <>
inline ark::tooling::VRegValue UnboxPrimitiveToVReg<EtsFloat>(EtsObject *obj)
{
    return ark::tooling::VRegValue(static_cast<int64_t>(bit_cast<int32_t>(EtsBoxPrimitive<EtsFloat>::Unbox(obj))));
}

template <>
inline ark::tooling::VRegValue UnboxPrimitiveToVReg<EtsDouble>(EtsObject *obj)
{
    return ark::tooling::VRegValue(static_cast<int64_t>(bit_cast<int64_t>(EtsBoxPrimitive<EtsDouble>::Unbox(obj))));
}

template <>
inline ark::tooling::VRegValue UnboxPrimitiveToVReg<EtsLong>(EtsObject *obj)
{
    return ark::tooling::VRegValue(static_cast<int64_t>(EtsBoxPrimitive<EtsLong>::Unbox(obj)));
}

ark::tooling::VRegValue UnboxToVRegValueBySig(EtsObject *obj, char sig);

inline ark::tooling::VRegValue EtsObjectToVRegValue(EtsObject *obj, std::string_view typeSignature)
{
    if (IsTaggedSignature(typeSignature)) {
        return EtsObjectToTaggedVRegValue(obj);
    }
    if (obj == nullptr || IsNullOrNullValue(obj)) {
        return ark::tooling::VRegValue(0);
    }
    if (IsRefSignature(typeSignature)) {
        return ark::tooling::VRegValue(reinterpret_cast<int64_t>(obj->GetCoreType()));
    }
    return UnboxToVRegValueBySig(obj, typeSignature[0]);
}

inline EtsObject *VRegValueToRefObject(ark::tooling::VRegValue vregValue)
{
    if (vregValue.GetValue() == 0) {
        return nullptr;
    }
    auto *obj = EtsObject::FromCoreType(reinterpret_cast<ObjectHeader *>(vregValue.GetValue()));
    return IsNullOrNullValue(obj) ? nullptr : obj;
}

template <typename T>
inline EtsObject *BoxPrimitiveFromVReg(ark::tooling::VRegValue vregValue, EtsExecutionContext *executionCtx);

template <>
inline EtsObject *BoxPrimitiveFromVReg<EtsBoolean>(ark::tooling::VRegValue vregValue, EtsExecutionContext *ctx)
{
    return EtsBoxPrimitive<EtsBoolean>::Create(ctx, static_cast<EtsBoolean>(vregValue.GetValue()))->AsObject();
}

template <>
inline EtsObject *BoxPrimitiveFromVReg<EtsByte>(ark::tooling::VRegValue vregValue, EtsExecutionContext *ctx)
{
    return EtsBoxPrimitive<EtsByte>::Create(ctx, static_cast<EtsByte>(vregValue.GetValue()))->AsObject();
}

template <>
inline EtsObject *BoxPrimitiveFromVReg<EtsShort>(ark::tooling::VRegValue vregValue, EtsExecutionContext *ctx)
{
    return EtsBoxPrimitive<EtsShort>::Create(ctx, static_cast<EtsShort>(vregValue.GetValue()))->AsObject();
}

template <>
inline EtsObject *BoxPrimitiveFromVReg<EtsChar>(ark::tooling::VRegValue vregValue, EtsExecutionContext *ctx)
{
    return EtsBoxPrimitive<EtsChar>::Create(ctx, static_cast<EtsChar>(vregValue.GetValue()))->AsObject();
}

template <>
inline EtsObject *BoxPrimitiveFromVReg<EtsInt>(ark::tooling::VRegValue vregValue, EtsExecutionContext *ctx)
{
    return EtsBoxPrimitive<EtsInt>::Create(ctx, static_cast<EtsInt>(vregValue.GetValue()))->AsObject();
}

template <>
inline EtsObject *BoxPrimitiveFromVReg<EtsFloat>(ark::tooling::VRegValue vregValue, EtsExecutionContext *ctx)
{
    return EtsBoxPrimitive<EtsFloat>::Create(ctx, bit_cast<EtsFloat>(static_cast<int32_t>(vregValue.GetValue())))
        ->AsObject();
}

template <>
inline EtsObject *BoxPrimitiveFromVReg<EtsDouble>(ark::tooling::VRegValue vregValue, EtsExecutionContext *ctx)
{
    return EtsBoxPrimitive<EtsDouble>::Create(ctx, bit_cast<EtsDouble>(vregValue.GetValue()))->AsObject();
}

template <>
inline EtsObject *BoxPrimitiveFromVReg<EtsLong>(ark::tooling::VRegValue vregValue, EtsExecutionContext *ctx)
{
    return EtsBoxPrimitive<EtsLong>::Create(ctx, static_cast<EtsLong>(vregValue.GetValue()))->AsObject();
}

EtsObject *BoxVRegValueBySig(ark::tooling::VRegValue vregValue, char sig, EtsExecutionContext *executionCtx);

inline EtsObject *BoxVRegValue(ark::tooling::VRegValue vregValue, char sig, EtsExecutionContext *executionCtx)
{
    return BoxVRegValueBySig(vregValue, sig, executionCtx);
}

inline EtsObject *VRegValueToEtsObject(ark::tooling::VRegValue vregValue, std::string_view typeSignature,
                                       EtsExecutionContext *executionCtx)
{
    if (IsTaggedSignature(typeSignature)) {
        return TaggedVRegValueToEtsObject(vregValue, executionCtx);
    }
    if (IsRefSignature(typeSignature)) {
        return VRegValueToRefObject(vregValue);
    }
    auto *boxed = BoxVRegValue(vregValue, typeSignature[0], executionCtx);
    return boxed != nullptr ? boxed : VRegValueToRefObject(vregValue);
}

}  // namespace ark::ets::tooling

#endif  // PANDA_PLUGINS_ETS_RUNTIME_TOOLING_HELPERS_H
