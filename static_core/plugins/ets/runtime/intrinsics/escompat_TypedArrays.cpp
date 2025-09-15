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

#include <charconv>
#include <optional>
#include <type_traits>
#include <cmath>
#include "ets_handle.h"
#include "libarkbase/utils/utf.h"
#include "libarkbase/utils/utils.h"
#include "plugins/ets/runtime/types/ets_typed_arrays.h"
#include "plugins/ets/runtime/types/ets_typed_unsigned_arrays.h"
#include "plugins/ets/runtime/intrinsics/helpers/ets_intrinsics_helpers.h"
#include "intrinsics.h"
#include "cross_values.h"
#include "types/ets_object.h"
#include "types/ets_primitives.h"

constexpr uint64_t DOUBLE_SIGNIFICAND_SIZE = 52;
constexpr uint64_t DOUBLE_EXPONENT_BIAS = 1023;
constexpr uint64_t DOUBLE_SIGN_MASK = 0x8000000000000000ULL;
constexpr uint64_t DOUBLE_EXPONENT_MASK = (0x7FFULL << DOUBLE_SIGNIFICAND_SIZE);
constexpr uint64_t DOUBLE_SIGNIFICAND_MASK = 0x000FFFFFFFFFFFFFULL;
constexpr uint64_t DOUBLE_HIDDEN_BIT = (1ULL << DOUBLE_SIGNIFICAND_SIZE);
#define INT64_BITS (64)
#define INT8_BITS (8)
#define INT16_BITS (16)
#define INT32_BITS (32)

namespace ark::ets::intrinsics {

template <typename T>
static void *GetNativeData(T *array)
{
    auto *arrayBuffer = static_cast<EtsEscompatArrayBuffer *>(&*array->GetBuffer());
    if (UNLIKELY(arrayBuffer->WasDetached())) {
        EtsCoroutine *coro = EtsCoroutine::GetCurrent();
        ThrowEtsException(coro, panda_file_items::class_descriptors::TYPE_ERROR, "ArrayBuffer was detached");
        return nullptr;
    }

    return arrayBuffer->GetData();
}

template <typename T>
static void EtsEscompatTypedArraySet(T *thisArray, EtsInt pos, typename T::ElementType val)
{
    auto *data = GetNativeData(thisArray);
    if (UNLIKELY(data == nullptr)) {
        return;
    }

    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    if (UNLIKELY(pos < 0 || pos >= thisArray->GetLengthInt())) {
        EtsCoroutine *coro = EtsCoroutine::GetCurrent();
        ThrowEtsException(coro, panda_file_items::class_descriptors::RANGE_ERROR, "invalid index");
        return;
    }
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    ObjectAccessor::SetPrimitive(
        // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
        data, pos * sizeof(typename T::ElementType) + static_cast<EtsInt>(thisArray->GetByteOffset()), val);
}

template <typename T>
typename T::ElementType EtsEscompatTypedArrayGet(T *thisArray, EtsInt pos)
{
    auto *data = GetNativeData(thisArray);
    if (UNLIKELY(data == nullptr)) {
        return 0;
    }

    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    if (UNLIKELY(pos < 0 || pos >= thisArray->GetLengthInt())) {
        EtsCoroutine *coro = EtsCoroutine::GetCurrent();
        ThrowEtsException(coro, panda_file_items::class_descriptors::RANGE_ERROR, "invalid index");
        return 0;
    }
    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    return ObjectAccessor::GetPrimitive<typename T::ElementType>(
        // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
        data, pos * sizeof(typename T::ElementType) + static_cast<EtsInt>(thisArray->GetByteOffset()));
}

template <typename T>
typename T::ElementType EtsEscompatTypedArrayGetUnsafe(T *thisArray, EtsInt pos)
{
    ASSERT(pos >= 0 && pos < thisArray->GetLengthInt());

    auto *data = GetNativeData(thisArray);
    if (UNLIKELY(data == nullptr)) {
        return 0;
    }

    return ObjectAccessor::GetPrimitive<typename T::ElementType>(
        data, pos * sizeof(typename T::ElementType) + static_cast<EtsInt>(thisArray->GetByteOffset()));
}

extern "C" void EtsEscompatInt8ArraySetInt(ark::ets::EtsEscompatInt8Array *thisArray, EtsInt pos, EtsInt val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" void EtsEscompatInt8ArraySetByte(ark::ets::EtsEscompatInt8Array *thisArray, EtsInt pos, EtsByte val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" EtsDouble EtsEscompatInt8ArrayGet(ark::ets::EtsEscompatInt8Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGet(thisArray, pos);
}

extern "C" EtsByte EtsEscompatInt8ArrayGetUnsafe(ark::ets::EtsEscompatInt8Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGetUnsafe(thisArray, pos);
}

// We can just copy the backing buffer if the types are the same, or if we are converting e.g. Int16 <-> Uint16, as
// the binary representation will be the same. Unfortunately, we cannot use this fact to convert Uint32 -> Int32,
// because in the already implemented method Int32Array.from(ArrayLike<number>), the Uint32 -> Double -> Int32
// conversion takes place actually and Double -> Int32 clips values to MAX_INT.
template <typename T, typename U>
struct CompatibleElements : std::false_type {
};

template <>
struct CompatibleElements<EtsEscompatInt8Array, EtsEscompatUInt8Array> : std::true_type {
};

template <>
struct CompatibleElements<EtsEscompatUInt8Array, EtsEscompatInt8Array> : std::true_type {
};

template <>
struct CompatibleElements<EtsEscompatInt16Array, EtsEscompatUInt16Array> : std::true_type {
};

template <>
struct CompatibleElements<EtsEscompatUInt16Array, EtsEscompatInt16Array> : std::true_type {
};

template <>
struct CompatibleElements<EtsEscompatUInt32Array, EtsEscompatInt32Array> : std::true_type {
};

template <typename T, typename S = T,
          typename = std::enable_if_t<std::disjunction_v<std::is_same<T, S>, CompatibleElements<T, S>>>>
static void EtsEscompatTypedArraySetValuesImpl(T *thisArray, S *srcArray, EtsInt pos)
{
    auto *dstData = GetNativeData(thisArray);
    if (UNLIKELY(dstData == nullptr)) {
        return;
    }
    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto *srcData = GetNativeData(srcArray);
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    if (UNLIKELY(srcData == nullptr || srcArray->GetLengthInt() == 0)) {
        return;
    }

    using ElementType = typename T::ElementType;
    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    if (UNLIKELY(pos < 0 || pos + srcArray->GetLengthInt() > thisArray->GetLengthInt())) {
        EtsCoroutine *coro = EtsCoroutine::GetCurrent();
        ThrowEtsException(coro, panda_file_items::class_descriptors::RANGE_ERROR, "offset is out of bounds");
        return;
    }
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto *dst = ToVoidPtr(ToUintPtr(dstData) + thisArray->GetByteOffset() + pos * sizeof(ElementType));
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto *src = ToVoidPtr(ToUintPtr(srcData) + srcArray->GetByteOffset());
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    [[maybe_unused]] auto error = memmove_s(dst, (thisArray->GetLengthInt() - pos) * sizeof(ElementType), src,
                                            // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
                                            srcArray->GetLengthInt() * sizeof(ElementType));
    ASSERT(error == EOK);
}

namespace {

namespace unbox {

constexpr uint64_t LONG_SHIFT = 32U;
constexpr uint64_t LONG_MASK = (uint64_t {1} << LONG_SHIFT) - uint64_t {1U};

[[nodiscard]] EtsLong GetLong(const EtsBigInt *bigint)
{
    const auto *bytes = bigint->GetBytes();
    ASSERT(bytes != nullptr);
    auto buff = EtsLong {0};
    const auto len = std::min(uint32_t {2U}, static_cast<uint32_t>(bytes->GetLength()));
    for (uint32_t i = 0; i < len; ++i) {
        buff += static_cast<EtsLong>((static_cast<uint32_t>(bytes->Get(i)) & LONG_MASK) << i * LONG_SHIFT);
    }
    return bigint->GetSign() * buff;
}

[[nodiscard]] EtsUlong GetULong(const EtsBigInt *bigint)
{
    return static_cast<EtsUlong>(GetLong(bigint));
}

template <typename Cond>
bool CheckCastedClass(Cond cond, const EtsClass *expectedClass, const EtsClass *objectClass)
{
    if (UNLIKELY(!cond(objectClass))) {
        ThrowClassCastException(expectedClass->GetRuntimeClass(), objectClass->GetRuntimeClass());
        return false;
    }
    return true;
}

template <typename T, typename = void>
class ArrayElement;

template <typename T>
class ArrayElement<T, std::enable_if_t<std::is_integral_v<typename T::ElementType>>> {
public:
    using Type = typename T::ElementType;

    ArrayElement() : coreIntClass_(PlatformTypes(EtsCoroutine::GetCurrent())->coreInt) {}

    std::optional<Type> GetTyped(EtsObject *object) const
    {
        const auto value = Unbox(object);
        return value.has_value() ? std::optional(ToTyped(*value)) : std::nullopt;
    }

private:
    std::optional<EtsInt> Unbox(EtsObject *object) const
    {
        if (CheckCastedClass([](const EtsClass *klass) { return klass->IsBoxedInt(); }, coreIntClass_,
                             object->GetClass())) {
            return EtsBoxPrimitive<EtsInt>::Unbox(object);
        }
        return std::nullopt;
    }

    static Type ToTyped(EtsInt value)
    {
        return static_cast<Type>(value);
    }

    const EtsClass *coreIntClass_;
};

template <typename T>
class ArrayElement<T, std::enable_if_t<std::is_floating_point_v<typename T::ElementType>>> {
public:
    using Type = typename T::ElementType;

    ArrayElement() : coreDoubleClass_(PlatformTypes(EtsCoroutine::GetCurrent())->coreDouble) {}

    std::optional<Type> GetTyped(EtsObject *object) const
    {
        const auto value = Unbox(object);
        return value.has_value() ? std::optional(ToTyped(*value)) : std::nullopt;
    }

private:
    std::optional<EtsDouble> Unbox(EtsObject *object) const
    {
        if (CheckCastedClass([](const EtsClass *klass) { return klass->IsBoxedDouble(); }, coreDoubleClass_,
                             object->GetClass())) {
            return EtsBoxPrimitive<EtsDouble>::Unbox(object);
        }
        return std::nullopt;
    }

    static Type ToTyped(EtsDouble value);

    const EtsClass *coreDoubleClass_;
};

template <>
auto ArrayElement<EtsEscompatInt32Array>::ToTyped(EtsInt value) -> Type
{
    return value;
}

template <>
auto ArrayElement<EtsEscompatUInt8ClampedArray>::ToTyped(EtsInt value) -> Type
{
    if (!(value > ark::ets::EtsEscompatUInt8ClampedArray::MIN)) {
        return ark::ets::EtsEscompatUInt8ClampedArray::MIN;
    }
    if (value > ark::ets::EtsEscompatUInt8ClampedArray::MAX) {
        return ark::ets::EtsEscompatUInt8ClampedArray::MAX;
    }
    return static_cast<uint8_t>(value);
}

template <>
auto ArrayElement<EtsEscompatFloat32Array>::ToTyped(EtsDouble value) -> Type
{
    return static_cast<Type>(value);
}

template <>
auto ArrayElement<EtsEscompatFloat64Array>::ToTyped(EtsDouble value) -> Type
{
    return value;
}

template <typename ArrayType>
class BigInt64ArrayElement {
public:
    using Type = typename ArrayType::ElementType;

    BigInt64ArrayElement() : escompatBigIntClass_(PlatformTypes(EtsCoroutine::GetCurrent())->escompatBigint) {}

    std::optional<Type> GetTyped(EtsObject *object) const
    {
        const auto *bigint = Cast(object);
        return bigint != nullptr ? std::optional(ArrayElement<ArrayType>::ToTyped(bigint)) : std::nullopt;
    }

private:
    const EtsBigInt *Cast(EtsObject *object) const
    {
        if (CheckCastedClass([](const EtsClass *klass) { return klass->IsBigInt(); }, escompatBigIntClass_,
                             object->GetClass())) {
            return EtsBigInt::FromEtsObject(object);
        }
        return nullptr;
    }

    static Type ToTyped([[maybe_unused]] const EtsBigInt *bigint);

    friend ArrayElement<ArrayType>;

    const EtsClass *escompatBigIntClass_;
};

template <>
class ArrayElement<EtsEscompatBigInt64Array> final : public BigInt64ArrayElement<EtsEscompatBigInt64Array> {
private:
    static Type ToTyped(const EtsBigInt *bigint)
    {
        ASSERT(bigint != nullptr);
        return GetLong(bigint);
    }
    friend BigInt64ArrayElement<EtsEscompatBigInt64Array>;
};

template <>
class ArrayElement<EtsEscompatBigUInt64Array> final : public BigInt64ArrayElement<EtsEscompatBigUInt64Array> {
private:
    static Type ToTyped(const EtsBigInt *bigint)
    {
        ASSERT(bigint != nullptr);
        return GetULong(bigint);
    }
    friend BigInt64ArrayElement<EtsEscompatBigUInt64Array>;
};

}  // namespace unbox
}  // namespace

template <typename T>
static void EtsEscompatTypedArraySetValuesFromArrayImpl(T *thisArray, EtsEscompatArray *srcArray)
{
    auto *dstData = GetNativeData(thisArray);
    if (UNLIKELY(dstData == nullptr)) {
        return;
    }
    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto *srcData = srcArray->GetData();
    if (UNLIKELY(srcData == nullptr || srcData->GetLength() == 0)) {
        return;
    }

    using ElementType = typename T::ElementType;
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    if (UNLIKELY(srcArray->GetActualLength() > static_cast<uint32_t>(thisArray->GetLengthInt()))) {
        EtsCoroutine *coro = EtsCoroutine::GetCurrent();
        ThrowEtsException(coro, panda_file_items::class_descriptors::RANGE_ERROR, "offset is out of bounds");
        return;
    }

    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto offset = static_cast<EtsInt>(thisArray->GetByteOffset());
    const auto arrayElement = unbox::ArrayElement<T>();
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    for (size_t i = 0; i < srcArray->GetActualLength(); ++i) {
        const auto val = arrayElement.GetTyped(srcData->Get(i));
        if (!val.has_value()) {
            break;
        }
        ObjectAccessor::SetPrimitive(dstData, offset, *val);
        offset += sizeof(ElementType);
    }
}

extern "C" void EtsEscompatInt8ArraySetValues(ark::ets::EtsEscompatInt8Array *thisArray,
                                              ark::ets::EtsEscompatInt8Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatInt8ArraySetValuesFromUnsigned(ark::ets::EtsEscompatInt8Array *thisArray,
                                                          ark::ets::EtsEscompatUInt8Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatInt8ArraySetValuesWithOffset(ark::ets::EtsEscompatInt8Array *thisArray,
                                                        ark::ets::EtsEscompatInt8Array *srcArray, EtsInt pos)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, pos);
}

extern "C" void EtsEscompatInt8ArraySetValuesFromArray(ark::ets::EtsEscompatInt8Array *thisArray,
                                                       ark::ets::EtsEscompatArray *srcArray)
{
    EtsEscompatTypedArraySetValuesFromArrayImpl(thisArray, srcArray);
}

template <typename T, typename V>
static void EtsEscompatTypedArrayFillInternal(T *thisArray, V val, EtsInt begin, EtsInt end)
{
    static_assert(sizeof(V) == sizeof(typename T::ElementType));
    auto *data = GetNativeData(thisArray);
    if (UNLIKELY(data == nullptr)) {
        return;
    }
    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto offset = static_cast<EtsInt>(thisArray->GetByteOffset()) + begin * sizeof(V);
    for (auto i = begin; i < end; ++i) {
        ObjectAccessor::SetPrimitive(data, offset, val);
        offset += sizeof(V);
    }
}

extern "C" void EtsEscompatInt8ArrayFillInternal(ark::ets::EtsEscompatInt8Array *thisArray, EtsByte val, EtsInt begin,
                                                 EtsInt end)
{
    EtsEscompatTypedArrayFillInternal(thisArray, val, begin, end);
}

extern "C" void EtsEscompatInt16ArraySetInt(ark::ets::EtsEscompatInt16Array *thisArray, EtsInt pos, EtsInt val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" void EtsEscompatInt16ArraySetShort(ark::ets::EtsEscompatInt16Array *thisArray, EtsInt pos, EtsShort val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" EtsDouble EtsEscompatInt16ArrayGet(ark::ets::EtsEscompatInt16Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGet(thisArray, pos);
}

extern "C" EtsShort EtsEscompatInt16ArrayGetUnsafe(ark::ets::EtsEscompatInt16Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGetUnsafe(thisArray, pos);
}

extern "C" void EtsEscompatInt16ArraySetValues(ark::ets::EtsEscompatInt16Array *thisArray,
                                               ark::ets::EtsEscompatInt16Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatInt16ArraySetValuesFromUnsigned(ark::ets::EtsEscompatInt16Array *thisArray,
                                                           ark::ets::EtsEscompatUInt16Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatInt16ArraySetValuesWithOffset(ark::ets::EtsEscompatInt16Array *thisArray,
                                                         ark::ets::EtsEscompatInt16Array *srcArray, EtsInt pos)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, pos);
}

extern "C" void EtsEscompatInt16ArraySetValuesFromArray(ark::ets::EtsEscompatInt16Array *thisArray,
                                                        ark::ets::EtsEscompatArray *srcArray)
{
    EtsEscompatTypedArraySetValuesFromArrayImpl(thisArray, srcArray);
}

extern "C" void EtsEscompatInt16ArrayFillInternal(ark::ets::EtsEscompatInt16Array *thisArray, EtsShort val,
                                                  EtsInt begin, EtsInt end)
{
    EtsEscompatTypedArrayFillInternal(thisArray, val, begin, end);
}

extern "C" void EtsEscompatInt32ArraySetInt(ark::ets::EtsEscompatInt32Array *thisArray, EtsInt pos, EtsInt val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" EtsDouble EtsEscompatInt32ArrayGet(ark::ets::EtsEscompatInt32Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGet(thisArray, pos);
}

extern "C" EtsInt EtsEscompatInt32ArrayGetUnsafe(ark::ets::EtsEscompatInt32Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGetUnsafe(thisArray, pos);
}

extern "C" void EtsEscompatInt32ArraySetValues(ark::ets::EtsEscompatInt32Array *thisArray,
                                               ark::ets::EtsEscompatInt32Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatInt32ArraySetValuesWithOffset(ark::ets::EtsEscompatInt32Array *thisArray,
                                                         ark::ets::EtsEscompatInt32Array *srcArray, EtsInt pos)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, pos);
}

extern "C" void EtsEscompatInt32ArraySetValuesFromArray(ark::ets::EtsEscompatInt32Array *thisArray,
                                                        ark::ets::EtsEscompatArray *srcArray)
{
    EtsEscompatTypedArraySetValuesFromArrayImpl(thisArray, srcArray);
}

extern "C" void EtsEscompatInt32ArrayFillInternal(ark::ets::EtsEscompatInt32Array *thisArray, EtsInt val, EtsInt begin,
                                                  EtsInt end)
{
    EtsEscompatTypedArrayFillInternal(thisArray, val, begin, end);
}

extern "C" void EtsEscompatBigInt64ArraySetLong(ark::ets::EtsEscompatBigInt64Array *thisArray, EtsInt pos, EtsLong val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" EtsLong EtsEscompatBigInt64ArrayGet(ark::ets::EtsEscompatBigInt64Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGet(thisArray, pos);
}

extern "C" EtsLong EtsEscompatBigInt64ArrayGetUnsafe(ark::ets::EtsEscompatBigInt64Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGetUnsafe(thisArray, pos);
}

extern "C" void EtsEscompatBigInt64ArraySetValues(ark::ets::EtsEscompatBigInt64Array *thisArray,
                                                  ark::ets::EtsEscompatBigInt64Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatBigInt64ArraySetValuesWithOffset(ark::ets::EtsEscompatBigInt64Array *thisArray,
                                                            ark::ets::EtsEscompatBigInt64Array *srcArray, EtsInt pos)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, pos);
}

extern "C" void EtsEscompatBigInt64ArraySetValuesFromArray(ark::ets::EtsEscompatBigInt64Array *thisArray,
                                                           ark::ets::EtsEscompatArray *srcArray)
{
    EtsEscompatTypedArraySetValuesFromArrayImpl(thisArray, srcArray);
}

extern "C" void EtsEscompatBigInt64ArrayFillInternal(ark::ets::EtsEscompatBigInt64Array *thisArray, EtsLong val,
                                                     EtsInt begin, EtsInt end)
{
    EtsEscompatTypedArrayFillInternal(thisArray, val, begin, end);
}

extern "C" void EtsEscompatFloat32ArraySetFloat(ark::ets::EtsEscompatFloat32Array *thisArray, EtsInt pos, EtsFloat val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" EtsDouble EtsEscompatFloat32ArrayGet(ark::ets::EtsEscompatFloat32Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGet(thisArray, pos);
}

extern "C" EtsFloat EtsEscompatFloat32ArrayGetUnsafe(ark::ets::EtsEscompatFloat32Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGetUnsafe(thisArray, pos);
}

extern "C" void EtsEscompatFloat32ArraySetValues(ark::ets::EtsEscompatFloat32Array *thisArray,
                                                 ark::ets::EtsEscompatFloat32Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatFloat32ArraySetValuesWithOffset(ark::ets::EtsEscompatFloat32Array *thisArray,
                                                           ark::ets::EtsEscompatFloat32Array *srcArray, EtsInt pos)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, pos);
}

extern "C" void EtsEscompatFloat32ArraySetValuesFromArray(ark::ets::EtsEscompatFloat32Array *thisArray,
                                                          ark::ets::EtsEscompatArray *srcArray)
{
    EtsEscompatTypedArraySetValuesFromArrayImpl(thisArray, srcArray);
}

extern "C" void EtsEscompatFloat32ArrayFillInternal(ark::ets::EtsEscompatFloat32Array *thisArray, EtsFloat val,
                                                    EtsInt begin, EtsInt end)
{
    EtsEscompatTypedArrayFillInternal(thisArray, val, begin, end);
}

extern "C" void EtsEscompatFloat32ArrayFillInternalInt(ark::ets::EtsEscompatFloat32Array *thisArray, int32_t val,
                                                       EtsInt begin, EtsInt end)
{
    EtsEscompatTypedArrayFillInternal(thisArray, val, begin, end);
}

extern "C" void EtsEscompatFloat64ArraySetDouble(ark::ets::EtsEscompatFloat64Array *thisArray, EtsInt pos,
                                                 EtsDouble val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" EtsDouble EtsEscompatFloat64ArrayGet(ark::ets::EtsEscompatFloat64Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGet(thisArray, pos);
}

extern "C" EtsDouble EtsEscompatFloat64ArrayGetUnsafe(ark::ets::EtsEscompatFloat64Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGetUnsafe(thisArray, pos);
}

extern "C" void EtsEscompatFloat64ArraySetValues(ark::ets::EtsEscompatFloat64Array *thisArray,
                                                 ark::ets::EtsEscompatFloat64Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatFloat64ArraySetValuesWithOffset(ark::ets::EtsEscompatFloat64Array *thisArray,
                                                           ark::ets::EtsEscompatFloat64Array *srcArray, EtsInt pos)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, pos);
}

extern "C" void EtsEscompatFloat64ArraySetValuesFromArray(ark::ets::EtsEscompatFloat64Array *thisArray,
                                                          ark::ets::EtsEscompatArray *srcArray)
{
    EtsEscompatTypedArraySetValuesFromArrayImpl(thisArray, srcArray);
}

extern "C" void EtsEscompatFloat64ArrayFillInternal(ark::ets::EtsEscompatFloat64Array *thisArray, EtsDouble val,
                                                    EtsInt begin, EtsInt end)
{
    EtsEscompatTypedArrayFillInternal(thisArray, val, begin, end);
}

extern "C" void EtsEscompatFloat64ArrayFillInternalInt(ark::ets::EtsEscompatFloat64Array *thisArray, int64_t val,
                                                       EtsInt begin, EtsInt end)
{
    EtsEscompatTypedArrayFillInternal(thisArray, val, begin, end);
}

extern "C" void EtsEscompatUInt8ClampedArraySetInt(ark::ets::EtsEscompatUInt8ClampedArray *thisArray, EtsInt pos,
                                                   EtsInt val)
{
    if (val > ark::ets::EtsEscompatUInt8ClampedArray::MAX) {
        val = ark::ets::EtsEscompatUInt8ClampedArray::MAX;
    } else if (val < ark::ets::EtsEscompatUInt8ClampedArray::MIN) {
        val = ark::ets::EtsEscompatUInt8ClampedArray::MIN;
    }
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" EtsDouble EtsEscompatUInt8ClampedArrayGet(ark::ets::EtsEscompatUInt8ClampedArray *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGet(thisArray, pos);
}

extern "C" EtsInt EtsEscompatUInt8ClampedArrayGetUnsafe(ark::ets::EtsEscompatUInt8ClampedArray *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGetUnsafe(thisArray, pos);
}

extern "C" void EtsEscompatUInt8ClampedArraySetValues(ark::ets::EtsEscompatUInt8ClampedArray *thisArray,
                                                      ark::ets::EtsEscompatUInt8ClampedArray *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatUInt8ClampedArraySetValuesWithOffset(ark::ets::EtsEscompatUInt8ClampedArray *thisArray,
                                                                ark::ets::EtsEscompatUInt8ClampedArray *srcArray,
                                                                EtsInt pos)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, pos);
}

extern "C" void EtsEscompatUint8ClampedArraySetValuesFromArray(ark::ets::EtsEscompatUInt8ClampedArray *thisArray,
                                                               ark::ets::EtsEscompatArray *srcArray)
{
    EtsEscompatTypedArraySetValuesFromArrayImpl(thisArray, srcArray);
}

extern "C" void EtsEscompatUInt8ClampedArrayFillInternal(ark::ets::EtsEscompatUInt8ClampedArray *thisArray, EtsInt val,
                                                         EtsInt begin, EtsInt end)
{
    using ElementType = ark::ets::EtsEscompatUInt8ClampedArray::ElementType;
    EtsEscompatTypedArrayFillInternal(thisArray, static_cast<ElementType>(val), begin, end);
}

extern "C" void EtsEscompatUInt8ArraySetInt(ark::ets::EtsEscompatUInt8Array *thisArray, EtsInt pos, EtsInt val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" EtsDouble EtsEscompatUInt8ArrayGet(ark::ets::EtsEscompatUInt8Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGet(thisArray, pos);
}

extern "C" EtsInt EtsEscompatUInt8ArrayGetUnsafe(ark::ets::EtsEscompatUInt8Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGetUnsafe(thisArray, pos);
}

extern "C" void EtsEscompatUInt8ArraySetValues(ark::ets::EtsEscompatUInt8Array *thisArray,
                                               ark::ets::EtsEscompatUInt8Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatUint8ArraySetValuesFromSigned(ark::ets::EtsEscompatUInt8Array *thisArray,
                                                         ark::ets::EtsEscompatInt8Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatUInt8ArraySetValuesWithOffset(ark::ets::EtsEscompatUInt8Array *thisArray,
                                                         ark::ets::EtsEscompatUInt8Array *srcArray, EtsInt pos)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, pos);
}

extern "C" void EtsEscompatUint8ArraySetValuesFromArray(ark::ets::EtsEscompatUInt8Array *thisArray,
                                                        ark::ets::EtsEscompatArray *srcArray)
{
    EtsEscompatTypedArraySetValuesFromArrayImpl(thisArray, srcArray);
}

extern "C" void EtsEscompatUInt8ArrayFillInternal(ark::ets::EtsEscompatUInt8Array *thisArray, EtsInt val, EtsInt begin,
                                                  EtsInt end)
{
    using ElementType = ark::ets::EtsEscompatUInt8Array::ElementType;
    EtsEscompatTypedArrayFillInternal(thisArray, static_cast<ElementType>(val), begin, end);
}

extern "C" void EtsEscompatUInt16ArraySetInt(ark::ets::EtsEscompatUInt16Array *thisArray, EtsInt pos, EtsInt val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" EtsDouble EtsEscompatUInt16ArrayGet(ark::ets::EtsEscompatUInt16Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGet(thisArray, pos);
}

extern "C" EtsInt EtsEscompatUInt16ArrayGetUnsafe(ark::ets::EtsEscompatUInt16Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGetUnsafe(thisArray, pos);
}

extern "C" void EtsEscompatUInt16ArraySetValues(ark::ets::EtsEscompatUInt16Array *thisArray,
                                                ark::ets::EtsEscompatUInt16Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatUint16ArraySetValuesFromSigned(ark::ets::EtsEscompatUInt16Array *thisArray,
                                                          ark::ets::EtsEscompatInt16Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatUInt16ArraySetValuesWithOffset(ark::ets::EtsEscompatUInt16Array *thisArray,
                                                          ark::ets::EtsEscompatUInt16Array *srcArray, EtsInt pos)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, pos);
}

extern "C" void EtsEscompatUint16ArraySetValuesFromArray(ark::ets::EtsEscompatUInt16Array *thisArray,
                                                         ark::ets::EtsEscompatArray *srcArray)
{
    EtsEscompatTypedArraySetValuesFromArrayImpl(thisArray, srcArray);
}

extern "C" void EtsEscompatUInt16ArrayFillInternal(ark::ets::EtsEscompatUInt16Array *thisArray, EtsInt val,
                                                   EtsInt begin, EtsInt end)
{
    using ElementType = ark::ets::EtsEscompatUInt16Array::ElementType;
    EtsEscompatTypedArrayFillInternal(thisArray, static_cast<ElementType>(val), begin, end);
}

extern "C" void EtsEscompatUInt32ArraySetInt(ark::ets::EtsEscompatUInt32Array *thisArray, EtsInt pos, EtsInt val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" void EtsEscompatUInt32ArraySetLong(ark::ets::EtsEscompatUInt32Array *thisArray, EtsInt pos, EtsLong val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" EtsDouble EtsEscompatUInt32ArrayGet(ark::ets::EtsEscompatUInt32Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGet(thisArray, pos);
}

extern "C" EtsLong EtsEscompatUInt32ArrayGetUnsafe(ark::ets::EtsEscompatUInt32Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGetUnsafe(thisArray, pos);
}

extern "C" void EtsEscompatUInt32ArraySetValues(ark::ets::EtsEscompatUInt32Array *thisArray,
                                                ark::ets::EtsEscompatUInt32Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatUint32ArraySetValuesFromSigned(ark::ets::EtsEscompatUInt32Array *thisArray,
                                                          ark::ets::EtsEscompatInt32Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatUInt32ArraySetValuesWithOffset(ark::ets::EtsEscompatUInt32Array *thisArray,
                                                          ark::ets::EtsEscompatUInt32Array *srcArray, EtsInt pos)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, pos);
}

extern "C" void EtsEscompatUint32ArraySetValuesFromArray(ark::ets::EtsEscompatUInt32Array *thisArray,
                                                         ark::ets::EtsEscompatArray *srcArray)
{
    EtsEscompatTypedArraySetValuesFromArrayImpl(thisArray, srcArray);
}

extern "C" void EtsEscompatUInt32ArrayFillInternal(ark::ets::EtsEscompatUInt32Array *thisArray, EtsLong val,
                                                   EtsInt begin, EtsInt end)
{
    using ElementType = ark::ets::EtsEscompatUInt32Array::ElementType;
    EtsEscompatTypedArrayFillInternal(thisArray, static_cast<ElementType>(val), begin, end);
}

extern "C" void EtsEscompatBigUInt64ArraySetInt(ark::ets::EtsEscompatBigUInt64Array *thisArray, EtsInt pos, EtsInt val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" void EtsEscompatBigUInt64ArraySetLong(ark::ets::EtsEscompatBigUInt64Array *thisArray, EtsInt pos,
                                                 EtsLong val)
{
    EtsEscompatTypedArraySet(thisArray, pos, val);
}

extern "C" EtsLong EtsEscompatBigUInt64ArrayGet(ark::ets::EtsEscompatBigUInt64Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGet(thisArray, pos);
}

extern "C" EtsLong EtsEscompatBigUInt64ArrayGetUnsafe(ark::ets::EtsEscompatBigUInt64Array *thisArray, EtsInt pos)
{
    return EtsEscompatTypedArrayGetUnsafe(thisArray, pos);
}

extern "C" void EtsEscompatBigUInt64ArraySetValues(ark::ets::EtsEscompatBigUInt64Array *thisArray,
                                                   ark::ets::EtsEscompatBigUInt64Array *srcArray)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, 0);
}

extern "C" void EtsEscompatBigUInt64ArraySetValuesWithOffset(ark::ets::EtsEscompatBigUInt64Array *thisArray,
                                                             ark::ets::EtsEscompatBigUInt64Array *srcArray, EtsInt pos)
{
    EtsEscompatTypedArraySetValuesImpl(thisArray, srcArray, pos);
}

extern "C" void EtsEscompatBigUint64ArraySetValuesFromArray(ark::ets::EtsEscompatBigUInt64Array *thisArray,
                                                            ark::ets::EtsEscompatArray *srcArray)
{
    EtsEscompatTypedArraySetValuesFromArrayImpl(thisArray, srcArray);
}

extern "C" void EtsEscompatBigUInt64ArrayFillInternal(ark::ets::EtsEscompatBigUInt64Array *thisArray, EtsLong val,
                                                      EtsInt begin, EtsInt end)
{
    EtsEscompatTypedArrayFillInternal(thisArray, val, begin, end);
}

/*
 * Typed Arrays: Reverse Implementation
 */
template <typename T>
static inline T *EtsEscompatTypedArrayReverseImpl(T *thisArray)
{
    auto *arrData = GetNativeData(thisArray);
    if (UNLIKELY(arrData == nullptr)) {
        return nullptr;
    }
    using ElementType = typename T::ElementType;
    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto *ptrFirst = reinterpret_cast<ElementType *>(ToVoidPtr(ToUintPtr(arrData) + thisArray->GetByteOffset()));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto *ptrLast = ptrFirst +
                    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
                    thisArray->GetLengthInt();
    std::reverse(ptrFirst, ptrLast);
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    return thisArray;
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define REVERSE_CALL_DECL(Type)                                                     \
    /* CC-OFFNXT(G.PRE.02) name part */                                             \
    extern "C" ark::ets::EtsEscompat##Type##Array *EtsEscompat##Type##ArrayReverse( \
        ark::ets::EtsEscompat##Type##Array *thisArray)                              \
    {                                                                               \
        /* CC-OFFNXT(G.PRE.05) function gen */                                      \
        return EtsEscompatTypedArrayReverseImpl(thisArray);                         \
    }  // namespace ark::ets::intrinsics

REVERSE_CALL_DECL(Int8)
REVERSE_CALL_DECL(Int16)
REVERSE_CALL_DECL(Int32)
REVERSE_CALL_DECL(BigInt64)
REVERSE_CALL_DECL(Float32)
REVERSE_CALL_DECL(Float64)

REVERSE_CALL_DECL(UInt8)
REVERSE_CALL_DECL(UInt8Clamped)
REVERSE_CALL_DECL(UInt16)
REVERSE_CALL_DECL(UInt32)
REVERSE_CALL_DECL(BigUInt64)

#undef REVERSE_CALL_DECL

/// @brief Creates a new ByteArray (for ArrayBuffer) with specified size. Should be nonMovable.
static inline EtsByteArray *CreateByteArrayNonMov(EtsInt byteLen)
{
    auto klass = EtsByteArray::GetComponentClass()->GetRuntimeClass();
    auto *array =
        reinterpret_cast<EtsByteArray *>(Thread::GetCurrent()->GetVM()->GetHeapManager()->AllocateNonMovableObject(
            klass, sizeof(EtsByteArray) + byteLen, DEFAULT_ALIGNMENT, ManagedThread::GetCurrent(),
            mem::ObjectAllocatorBase::ObjMemInitPolicy::NO_INIT));
    array->UpdateLength(byteLen);
    return array;
}

/// @brief Creates a new ArrayBuffer with specified ByteArray and size.
static inline EtsEscompatArrayBuffer *CreateArrayBufferExternalData(EtsInt byteLen, EtsByteArray *externalData)
{
    auto *klass = PlatformTypes(EtsCoroutine::GetCurrent())->escompatArrayBuffer->GetRuntimeClass();
    auto *buffer =
        reinterpret_cast<EtsEscompatArrayBuffer *>(Thread::GetCurrent()->GetVM()->GetHeapManager()->AllocateObject(
            klass, sizeof(EtsEscompatArrayBuffer), DEFAULT_ALIGNMENT, ManagedThread::GetCurrent(),
            mem::ObjectAllocatorBase::ObjMemInitPolicy::NO_INIT, false));
    buffer->Initialize(EtsCoroutine::GetCurrent(), byteLen, externalData);
    return buffer;
}

/*
 * Typed Arrays: reversed alloc data implementation: needs for toReversed().
 */
template <typename T>
static EtsEscompatArrayBuffer *EtsEscompatTypedArrayAllocReversedBuffer(T *thisArray)
{
    using ElementType = typename T::ElementType;
    const EtsInt srcLength = thisArray->GetLengthInt();
    const ElementType *src = static_cast<ElementType *>(GetNativeData(thisArray));
    const EtsInt lenBytes = srcLength * sizeof(ElementType);
    EtsByteArray *data = CreateByteArrayNonMov(lenBytes);
    if (UNLIKELY(src == nullptr || data == nullptr)) {
        return nullptr;
    }
    auto *dest = data->GetData<ElementType>();
    ASSERT(dest != nullptr);
    for (int i = 0; i < srcLength; i++) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic, clang-analyzer-core.NullDereference)
        dest[i] = src[(srcLength - 1) - i];
    }
    return CreateArrayBufferExternalData(lenBytes, data);
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ALLOC_REVERSED_BUFFER_CALL_DECL(Type)                                       \
    /* CC-OFFNXT(G.PRE.02) name part */                                             \
    extern "C" EtsEscompatArrayBuffer *EtsEscompat##Type##ArrayAllocReversedBuffer( \
        ark::ets::EtsEscompat##Type##Array *thisArray)                              \
    {                                                                               \
        /* CC-OFFNXT(G.PRE.05) function gen */                                      \
        return EtsEscompatTypedArrayAllocReversedBuffer(thisArray);                 \
    }

ALLOC_REVERSED_BUFFER_CALL_DECL(Int8)
ALLOC_REVERSED_BUFFER_CALL_DECL(Int16)
ALLOC_REVERSED_BUFFER_CALL_DECL(Int32)
ALLOC_REVERSED_BUFFER_CALL_DECL(BigInt64)
ALLOC_REVERSED_BUFFER_CALL_DECL(Float32)
ALLOC_REVERSED_BUFFER_CALL_DECL(Float64)

ALLOC_REVERSED_BUFFER_CALL_DECL(UInt8)
ALLOC_REVERSED_BUFFER_CALL_DECL(UInt8Clamped)
ALLOC_REVERSED_BUFFER_CALL_DECL(UInt16)
ALLOC_REVERSED_BUFFER_CALL_DECL(UInt32)
ALLOC_REVERSED_BUFFER_CALL_DECL(BigUInt64)

#undef ALLOC_REVERSED_BUFFER_CALL_DECL

/*
 * Typed Arrays: alloc data with insert value to pos implementation: needs for with().
 */
template <typename T>
static EtsEscompatArrayBuffer *EtsEscompatTypedArrayAllocBufferWith(T *thisArray, EtsInt pos,
                                                                    typename T::ElementType val)
{
    using ElementType = typename T::ElementType;
    const EtsInt srcLength = thisArray->GetLengthInt();
    if (UNLIKELY(pos < 0 || pos >= srcLength)) {
        ThrowEtsException(EtsCoroutine::GetCurrent(), panda_file_items::class_descriptors::RANGE_ERROR,
                          "offset is out of bounds");
        return nullptr;
    }
    const ElementType *src = static_cast<ElementType *>(GetNativeData(thisArray));
    const EtsInt lenBytes = srcLength * sizeof(ElementType);
    EtsByteArray *data = CreateByteArrayNonMov(lenBytes);
    if (UNLIKELY(src == nullptr || data == nullptr)) {
        return nullptr;
    }
    auto *dest = data->GetData<ElementType>();
    ASSERT(dest != nullptr);
    if (LIKELY(lenBytes > 0)) {
        [[maybe_unused]] auto err = memcpy_s(dest, lenBytes, src, lenBytes);
        ASSERT(err == EOK);
    }
    // NOLINTNEXTLINE (cppcoreguidelines-pro-bounds-pointer-arithmetic)
    dest[pos] = val;
    return CreateArrayBufferExternalData(lenBytes, data);
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ALLOC_BUFFER_WITH_CALL_DECL(AType, EType)                                \
    /* CC-OFFNXT(G.PRE.02) name part */                                          \
    extern "C" EtsEscompatArrayBuffer *EtsEscompat##AType##ArrayAllocBufferWith( \
        ark::ets::EtsEscompat##AType##Array *thisArray, EtsInt pos, EType val)   \
    {                                                                            \
        /* CC-OFFNXT(G.PRE.05) function gen */                                   \
        return EtsEscompatTypedArrayAllocBufferWith(thisArray, pos, val);        \
    }

extern "C" EtsEscompatArrayBuffer *EtsEscompatUInt8ClampedArrayAllocBufferWith(
    ark::ets::EtsEscompatUInt8ClampedArray *thisArray, EtsInt pos, EtsInt val)
{
    if (val > ark::ets::EtsEscompatUInt8ClampedArray::MAX) {
        val = ark::ets::EtsEscompatUInt8ClampedArray::MAX;
    } else if (val < ark::ets::EtsEscompatUInt8ClampedArray::MIN) {
        val = ark::ets::EtsEscompatUInt8ClampedArray::MIN;
    }
    return EtsEscompatTypedArrayAllocBufferWith(thisArray, pos, val);
}

ALLOC_BUFFER_WITH_CALL_DECL(Int8, EtsByte)
ALLOC_BUFFER_WITH_CALL_DECL(Int16, EtsShort)
ALLOC_BUFFER_WITH_CALL_DECL(Int32, EtsInt)
ALLOC_BUFFER_WITH_CALL_DECL(BigInt64, EtsLong)
ALLOC_BUFFER_WITH_CALL_DECL(Float32, EtsFloat)
ALLOC_BUFFER_WITH_CALL_DECL(Float64, EtsDouble)
ALLOC_BUFFER_WITH_CALL_DECL(UInt8, EtsInt)
ALLOC_BUFFER_WITH_CALL_DECL(UInt16, EtsInt)
ALLOC_BUFFER_WITH_CALL_DECL(UInt32, EtsLong)
ALLOC_BUFFER_WITH_CALL_DECL(BigUInt64, EtsLong)

#undef ALLOC_BUFFER_WITH_CALL_DECL

static inline int32_t NormalizeIndex(int32_t idx, int32_t arrayLength)
{
    if (idx < -arrayLength) {
        return 0;
    }
    if (idx < 0) {
        return arrayLength + idx;
    }
    if (idx > arrayLength) {
        return arrayLength;
    }
    return idx;
}

/*
 * Typed Arrays: sliced alloc data implementation: needs for slice().
 */
template <typename T>
static EtsEscompatArrayBuffer *EtsEscompatTypedArrayAllocSlicedBufferImpl(T *thisArray, EtsInt beg, EtsInt end)
{
    using ElementType = typename T::ElementType;
    const EtsInt srcLength = thisArray->GetLengthInt();
    beg = NormalizeIndex(beg, srcLength);
    end = NormalizeIndex(end, srcLength);
    const EtsInt lengthInt = beg <= end ? end - beg : 0;
    // NOLINTNEXTLINE (cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const ElementType *src = static_cast<ElementType *>(GetNativeData(thisArray)) + beg;
    const EtsInt lenBytes = lengthInt * sizeof(ElementType);
    EtsByteArray *data = CreateByteArrayNonMov(lenBytes);
    if (UNLIKELY(src == nullptr || data == nullptr)) {
        return nullptr;
    }
    auto *dest = data->GetData<ElementType>();
    ASSERT(dest != nullptr);
    if (LIKELY(lenBytes > 0)) {
        [[maybe_unused]] auto err = memcpy_s(dest, lenBytes, src, lenBytes);
        ASSERT(err == EOK);
    }
    return CreateArrayBufferExternalData(lenBytes, data);
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ALLOC_SLICED_BUFFER_CALL_DECL(Type)                                       \
    /* CC-OFFNXT(G.PRE.02) name part */                                           \
    extern "C" EtsEscompatArrayBuffer *EtsEscompat##Type##ArrayAllocSlicedBuffer( \
        ark::ets::EtsEscompat##Type##Array *thisArray, EtsInt beg, EtsInt end)    \
    {                                                                             \
        /* CC-OFFNXT(G.PRE.05) function gen */                                    \
        return EtsEscompatTypedArrayAllocSlicedBufferImpl(thisArray, beg, end);   \
    }

ALLOC_SLICED_BUFFER_CALL_DECL(Int8)
ALLOC_SLICED_BUFFER_CALL_DECL(Int16)
ALLOC_SLICED_BUFFER_CALL_DECL(Int32)
ALLOC_SLICED_BUFFER_CALL_DECL(BigInt64)
ALLOC_SLICED_BUFFER_CALL_DECL(Float32)
ALLOC_SLICED_BUFFER_CALL_DECL(Float64)

ALLOC_SLICED_BUFFER_CALL_DECL(UInt8)
ALLOC_SLICED_BUFFER_CALL_DECL(UInt8Clamped)
ALLOC_SLICED_BUFFER_CALL_DECL(UInt16)
ALLOC_SLICED_BUFFER_CALL_DECL(UInt32)
ALLOC_SLICED_BUFFER_CALL_DECL(BigUInt64)

#undef ALLOC_SLICED_BUFFER_CALL_DECL

template <typename T>
T *EtsEscompatTypedArrayCopyWithinImpl(T *thisArray, EtsInt target, EtsInt start, EtsInt end)
{
    auto *data = GetNativeData(thisArray);
    if (UNLIKELY(data == nullptr)) {
        return nullptr;
    }

    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto arrayLength = thisArray->GetLengthInt();
    target = NormalizeIndex(target, arrayLength);
    start = NormalizeIndex(start, arrayLength);
    end = NormalizeIndex(end, arrayLength);

    int32_t count = end - start;
    if (count > (arrayLength - target)) {
        count = arrayLength - target;
    }
    if (count <= 0) {
        // See (GetNativeData) reason
        // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
        return thisArray;
    }

    using ElementType = typename T::ElementType;
    // See (GetNativeData) reason
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto *targetAddress = ToVoidPtr(ToUintPtr(data) + thisArray->GetByteOffset() + target * sizeof(ElementType));
    // See (GetNativeData) reason
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto *startAddress = ToVoidPtr(ToUintPtr(data) + thisArray->GetByteOffset() + start * sizeof(ElementType));
    [[maybe_unused]] auto error = memmove_s(targetAddress, (arrayLength - start) * sizeof(ElementType), startAddress,
                                            count * sizeof(ElementType));
    ASSERT(error == EOK);
    // See (GetNativeData) reason
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    return thisArray;
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ETS_ESCOMPAT_COPY_WITHIN(Type)                                                          \
    /* CC-OFFNXT(G.PRE.02) name part */                                                         \
    extern "C" ark::ets::EtsEscompat##Type##Array *EtsEscompat##Type##ArrayCopyWithinImpl(      \
        ark::ets::EtsEscompat##Type##Array *thisArray, EtsInt target, EtsInt start, EtsInt end) \
    {                                                                                           \
        /* CC-OFFNXT(G.PRE.05) function gen */                                                  \
        return EtsEscompatTypedArrayCopyWithinImpl(thisArray, target, start, end);              \
    }  // namespace ark::ets::intrinsics

ETS_ESCOMPAT_COPY_WITHIN(Int8)
ETS_ESCOMPAT_COPY_WITHIN(Int16)
ETS_ESCOMPAT_COPY_WITHIN(Int32)
ETS_ESCOMPAT_COPY_WITHIN(BigInt64)
ETS_ESCOMPAT_COPY_WITHIN(Float32)
ETS_ESCOMPAT_COPY_WITHIN(Float64)
ETS_ESCOMPAT_COPY_WITHIN(UInt8)
ETS_ESCOMPAT_COPY_WITHIN(UInt16)
ETS_ESCOMPAT_COPY_WITHIN(UInt32)
ETS_ESCOMPAT_COPY_WITHIN(BigUInt64)
ETS_ESCOMPAT_COPY_WITHIN(UInt8Clamped)

#undef ETS_ESCOMPAT_COPY_WITHIN

template <typename T>
bool Cmp(const typename T::ElementType &left, const typename T::ElementType &right)
{
    const auto lNumber = static_cast<double>(left);
    const auto rNumber = static_cast<double>(right);
    if (std::isnan(lNumber)) {
        return false;
    }
    if (std::isnan(rNumber)) {
        return true;
    }
    return left < right;
}

template <typename T>
T *EtsEscompatTypedArraySortWrapper(T *thisArray, bool withNaN = false)
{
    using ElementType = typename T::ElementType;

    auto nBytes = static_cast<size_t>(thisArray->GetByteLength());
    ASSERT(nBytes == (static_cast<size_t>(thisArray->GetLengthInt()) * sizeof(ElementType)));
    // If it's an empty or one-element array then nothing to sort.
    if (UNLIKELY(nBytes <= sizeof(ElementType))) {
        return thisArray;
    }

    auto *arrayBuffer = static_cast<EtsEscompatArrayBuffer *>(&*thisArray->GetBuffer());
    if (UNLIKELY(arrayBuffer->WasDetached())) {
        auto coro = EtsCoroutine::GetCurrent();
        [[maybe_unused]] EtsHandleScope scope(coro);
        EtsHandle<EtsObject> handle(coro, thisArray);
        ThrowEtsException(coro, panda_file_items::class_descriptors::TYPE_ERROR, "ArrayBuffer was detached");
        return static_cast<T *>(handle.GetPtr());
    }
    void *dataPtr = arrayBuffer->GetData();
    ASSERT(dataPtr != nullptr);

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    Span<EtsByte> data(static_cast<EtsByte *>(dataPtr) + static_cast<size_t>(thisArray->GetByteOffset()), nBytes);
    if (withNaN) {
        std::sort(reinterpret_cast<ElementType *>(data.begin()), reinterpret_cast<ElementType *>(data.end()), Cmp<T>);
    } else {
        std::sort(reinterpret_cast<ElementType *>(data.begin()), reinterpret_cast<ElementType *>(data.end()));
    }
    return thisArray;
}

template <typename T>
T *EtsEscompatTypedArraySortWithNaN(T *thisArray)
{
    return EtsEscompatTypedArraySortWrapper(thisArray, true);
}

template <typename T>
T *EtsEscompatTypedArraySort(T *thisArray)
{
    return EtsEscompatTypedArraySortWrapper(thisArray);
}

extern "C" ark::ets::EtsEscompatInt8Array *EtsEscompatInt8ArraySort(ark::ets::EtsEscompatInt8Array *thisArray)
{
    return EtsEscompatTypedArraySort(thisArray);
}

extern "C" ark::ets::EtsEscompatInt16Array *EtsEscompatInt16ArraySort(ark::ets::EtsEscompatInt16Array *thisArray)
{
    return EtsEscompatTypedArraySort(thisArray);
}

extern "C" ark::ets::EtsEscompatInt32Array *EtsEscompatInt32ArraySort(ark::ets::EtsEscompatInt32Array *thisArray)
{
    return EtsEscompatTypedArraySort(thisArray);
}

extern "C" ark::ets::EtsEscompatBigInt64Array *EtsEscompatBigInt64ArraySort(
    ark::ets::EtsEscompatBigInt64Array *thisArray)
{
    return EtsEscompatTypedArraySort(thisArray);
}

extern "C" ark::ets::EtsEscompatFloat32Array *EtsEscompatFloat32ArraySort(ark::ets::EtsEscompatFloat32Array *thisArray)
{
    return EtsEscompatTypedArraySortWithNaN(thisArray);
}

extern "C" ark::ets::EtsEscompatFloat64Array *EtsEscompatFloat64ArraySort(ark::ets::EtsEscompatFloat64Array *thisArray)
{
    return EtsEscompatTypedArraySortWithNaN(thisArray);
}

template <typename Array, typename = std::enable_if_t<std::is_floating_point_v<typename Array::ElementType>>>
static bool EtsEscompatTypedArrayContainsNaN(Array *array, EtsInt pos)
{
    using ElementType = typename Array::ElementType;
    auto length = array->GetLengthInt();
    auto byteOffset = static_cast<size_t>(array->GetByteOffset());
    uint32_t normalIndex = NormalizeIndex(pos, length);
    auto *data = GetNativeData(array);
    auto *begin = ToVoidPtr(ToUintPtr(data) + byteOffset);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    Span<ElementType> span(static_cast<ElementType *>(begin) + normalIndex, length - normalIndex);
    return std::any_of(span.begin(), span.end(), [](ElementType elem) { return elem != elem; });
}

extern "C" EtsBoolean EtsEscompatFloat32ArrayContainsNaN(ark::ets::EtsEscompatFloat32Array *thisArray, EtsInt pos)
{
    return ToEtsBoolean(EtsEscompatTypedArrayContainsNaN(thisArray, pos));
}

extern "C" EtsBoolean EtsEscompatFloat64ArrayContainsNaN(ark::ets::EtsEscompatFloat64Array *thisArray, EtsInt pos)
{
    return ToEtsBoolean(EtsEscompatTypedArrayContainsNaN(thisArray, pos));
}

extern "C" ark::ets::EtsEscompatUInt8ClampedArray *EtsEscompatUInt8ClampedArraySort(
    ark::ets::EtsEscompatUInt8ClampedArray *thisArray)
{
    return EtsEscompatTypedArraySort(thisArray);
}

extern "C" ark::ets::EtsEscompatUInt8Array *EtsEscompatUInt8ArraySort(ark::ets::EtsEscompatUInt8Array *thisArray)
{
    return EtsEscompatTypedArraySort(thisArray);
}

extern "C" ark::ets::EtsEscompatUInt16Array *EtsEscompatUInt16ArraySort(ark::ets::EtsEscompatUInt16Array *thisArray)
{
    return EtsEscompatTypedArraySort(thisArray);
}

extern "C" ark::ets::EtsEscompatUInt32Array *EtsEscompatUInt32ArraySort(ark::ets::EtsEscompatUInt32Array *thisArray)
{
    return EtsEscompatTypedArraySort(thisArray);
}

extern "C" ark::ets::EtsEscompatBigUInt64Array *EtsEscompatBigUInt64ArraySort(
    ark::ets::EtsEscompatBigUInt64Array *thisArray)
{
    return EtsEscompatTypedArraySort(thisArray);
}

#define INVALID_INDEX (-1)

template <typename T, typename Pred>
static EtsInt EtsEscompatArrayIndexOf(T *thisArray, EtsInt fromIndex, Pred pred)
{
    auto *data = GetNativeData(thisArray);
    if (UNLIKELY(data == nullptr)) {
        return INVALID_INDEX;
    }

    using ElementType = typename T::ElementType;
    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto *array = reinterpret_cast<ElementType *>(ToUintPtr(data) + static_cast<int>(thisArray->GetByteOffset()));
    // See (GetNativeData) reason
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto arrayLength = thisArray->GetLengthInt();
    fromIndex = NormalizeIndex(fromIndex, arrayLength);
    Span<ElementType> span(static_cast<ElementType *>(array), arrayLength);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto idxIt = std::find_if(span.begin() + fromIndex, span.end(), pred);
    return idxIt == span.end() ? INVALID_INDEX : std::distance(span.begin(), idxIt);
}

template <typename T, typename SE>
static EtsInt EtsEscompatArrayIndexOfLong(T *thisArray, SE searchElement, EtsInt fromIndex)
{
    using ElementType = typename T::ElementType;
    return EtsEscompatArrayIndexOf(
        thisArray, fromIndex,
        [element = static_cast<ElementType>(searchElement)](ElementType e) { return e == element; });
}

template <typename T>
static EtsInt EtsEscompatArrayIndexOfNumber(T *thisArray, double searchElement, EtsInt fromIndex)
{
    if (std::isnan(searchElement)) {
        return INVALID_INDEX;
    }

    auto *data = GetNativeData(thisArray);
    if (UNLIKELY(data == nullptr)) {
        return INVALID_INDEX;
    }

    using ElementType = typename T::ElementType;
    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto *array = reinterpret_cast<ElementType *>(ToUintPtr(data) + static_cast<int>(thisArray->GetByteOffset()));
    // See (GetNativeData) reason
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto arrayLength = thisArray->GetLengthInt();
    fromIndex = NormalizeIndex(fromIndex, arrayLength);
    for (int i = fromIndex; i < arrayLength; i++) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        if (static_cast<double>(array[i]) == searchElement) {
            return i;
        }
    }
    return INVALID_INDEX;
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ETS_ESCOMPAT_INDEX_OF_NUMBER(Type)                                                                 \
    /* CC-OFFNXT(G.PRE.02) name part */                                                                    \
    extern "C" EtsInt EtsEscompat##Type##ArrayIndexOfNumber(ark::ets::EtsEscompat##Type##Array *thisArray, \
                                                            EtsDouble searchElement, EtsInt fromIndex)     \
    {                                                                                                      \
        /* CC-OFFNXT(G.PRE.05) function gen */                                                             \
        return EtsEscompatArrayIndexOfNumber(thisArray, searchElement, fromIndex);                         \
    }  // namespace ark::ets::intrinsics

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ETS_ESCOMPAT_INDEX_OF_LONG(Type)                                                                 \
    /* CC-OFFNXT(G.PRE.02) name part */                                                                  \
    extern "C" EtsInt EtsEscompat##Type##ArrayIndexOfLong(ark::ets::EtsEscompat##Type##Array *thisArray, \
                                                          EtsLong searchElement, EtsInt fromIndex)       \
    {                                                                                                    \
        /* CC-OFFNXT(G.PRE.05) function gen */                                                           \
        return EtsEscompatArrayIndexOfLong(thisArray, searchElement, fromIndex);                         \
    }  // namespace ark::ets::intrinsics

ETS_ESCOMPAT_INDEX_OF_NUMBER(Int8)
ETS_ESCOMPAT_INDEX_OF_NUMBER(Int16)
ETS_ESCOMPAT_INDEX_OF_NUMBER(Int32)
ETS_ESCOMPAT_INDEX_OF_NUMBER(BigInt64)
ETS_ESCOMPAT_INDEX_OF_NUMBER(Float32)
ETS_ESCOMPAT_INDEX_OF_NUMBER(Float64)
ETS_ESCOMPAT_INDEX_OF_NUMBER(UInt8)
ETS_ESCOMPAT_INDEX_OF_NUMBER(UInt16)
ETS_ESCOMPAT_INDEX_OF_NUMBER(UInt32)
ETS_ESCOMPAT_INDEX_OF_NUMBER(BigUInt64)
ETS_ESCOMPAT_INDEX_OF_NUMBER(UInt8Clamped)
ETS_ESCOMPAT_INDEX_OF_LONG(Int8)
ETS_ESCOMPAT_INDEX_OF_LONG(Int16)
ETS_ESCOMPAT_INDEX_OF_LONG(Int32)
ETS_ESCOMPAT_INDEX_OF_LONG(BigInt64)
ETS_ESCOMPAT_INDEX_OF_LONG(Float32)
ETS_ESCOMPAT_INDEX_OF_LONG(Float64)
ETS_ESCOMPAT_INDEX_OF_LONG(UInt8)
ETS_ESCOMPAT_INDEX_OF_LONG(UInt16)
ETS_ESCOMPAT_INDEX_OF_LONG(UInt32)
ETS_ESCOMPAT_INDEX_OF_LONG(BigUInt64)
ETS_ESCOMPAT_INDEX_OF_LONG(UInt8Clamped)

template <typename T1, typename T2>
static EtsInt EtsEscompatArrayLastIndexOfLong(T1 *thisArray, T2 searchElement, EtsInt fromIndex)
{
    auto *data = GetNativeData(thisArray);
    if (UNLIKELY(data == nullptr)) {
        return INVALID_INDEX;
    }

    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    int arrayLength = thisArray->GetLengthInt();
    if (arrayLength == 0) {
        return INVALID_INDEX;
    }

    int startIndex = arrayLength + fromIndex;
    if (fromIndex >= 0) {
        startIndex = (arrayLength - 1 < fromIndex) ? arrayLength - 1 : fromIndex;
    }

    using ElementType = typename T1::ElementType;
    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto *array = reinterpret_cast<ElementType *>(ToUintPtr(data) + static_cast<int>(thisArray->GetByteOffset()));
    auto element = static_cast<ElementType>(searchElement);
    for (int i = startIndex; i >= 0; i--) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        if (array[i] == element) {
            return i;
        }
    }

    return INVALID_INDEX;
}

template <typename T>
static EtsInt EtsEscompatArrayLastIndexOfNumber(T *thisArray, double searchElement, EtsInt fromIndex)
{
    if (std::isnan(searchElement)) {
        return INVALID_INDEX;
    }

    auto *data = GetNativeData(thisArray);
    if (UNLIKELY(data == nullptr)) {
        return INVALID_INDEX;
    }

    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    int arrayLength = thisArray->GetLengthInt();
    if (arrayLength == 0) {
        return INVALID_INDEX;
    }

    int startIndex = arrayLength + fromIndex;
    if (fromIndex >= 0) {
        startIndex = (arrayLength - 1 < fromIndex) ? arrayLength - 1 : fromIndex;
    }

    using ElementType = typename T::ElementType;
    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    auto *array = reinterpret_cast<ElementType *>(ToUintPtr(data) + static_cast<int>(thisArray->GetByteOffset()));
    for (int i = startIndex; i >= 0; i--) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        if (static_cast<double>(array[i]) == searchElement) {
            return i;
        }
    }

    return INVALID_INDEX;
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ETS_ESCOMPAT_LAST_INDEX_OF_NUMBER(Type)                                                                \
    /* CC-OFFNXT(G.PRE.02) name part */                                                                        \
    extern "C" EtsInt EtsEscompat##Type##ArrayLastIndexOfNumber(ark::ets::EtsEscompat##Type##Array *thisArray, \
                                                                EtsDouble searchElement, EtsInt fromIndex)     \
    {                                                                                                          \
        /* CC-OFFNXT(G.PRE.05) function gen */                                                                 \
        return EtsEscompatArrayLastIndexOfNumber(thisArray, searchElement, fromIndex);                         \
    }  // namespace ark::ets::intrinsics

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ETS_ESCOMPAT_LAST_INDEX_OF_LONG(Type)                                                                \
    /* CC-OFFNXT(G.PRE.02) name part */                                                                      \
    extern "C" EtsInt EtsEscompat##Type##ArrayLastIndexOfLong(ark::ets::EtsEscompat##Type##Array *thisArray, \
                                                              EtsLong searchElement, EtsInt fromIndex)       \
    {                                                                                                        \
        /* CC-OFFNXT(G.PRE.05) function gen */                                                               \
        return EtsEscompatArrayLastIndexOfLong(thisArray, searchElement, fromIndex);                         \
    }  // namespace ark::ets::intrinsics

ETS_ESCOMPAT_LAST_INDEX_OF_NUMBER(Int8)
ETS_ESCOMPAT_LAST_INDEX_OF_NUMBER(Int16)
ETS_ESCOMPAT_LAST_INDEX_OF_NUMBER(Int32)
ETS_ESCOMPAT_LAST_INDEX_OF_NUMBER(BigInt64)
ETS_ESCOMPAT_LAST_INDEX_OF_NUMBER(Float32)
ETS_ESCOMPAT_LAST_INDEX_OF_NUMBER(Float64)
ETS_ESCOMPAT_LAST_INDEX_OF_NUMBER(UInt8)
ETS_ESCOMPAT_LAST_INDEX_OF_NUMBER(UInt16)
ETS_ESCOMPAT_LAST_INDEX_OF_NUMBER(UInt32)
ETS_ESCOMPAT_LAST_INDEX_OF_NUMBER(BigUInt64)
ETS_ESCOMPAT_LAST_INDEX_OF_NUMBER(UInt8Clamped)
ETS_ESCOMPAT_LAST_INDEX_OF_LONG(Int8)
ETS_ESCOMPAT_LAST_INDEX_OF_LONG(Int16)
ETS_ESCOMPAT_LAST_INDEX_OF_LONG(Int32)
ETS_ESCOMPAT_LAST_INDEX_OF_LONG(BigInt64)
ETS_ESCOMPAT_LAST_INDEX_OF_LONG(Float32)
ETS_ESCOMPAT_LAST_INDEX_OF_LONG(Float64)
ETS_ESCOMPAT_LAST_INDEX_OF_LONG(UInt8)
ETS_ESCOMPAT_LAST_INDEX_OF_LONG(UInt16)
ETS_ESCOMPAT_LAST_INDEX_OF_LONG(UInt32)
ETS_ESCOMPAT_LAST_INDEX_OF_LONG(BigUInt64)
ETS_ESCOMPAT_LAST_INDEX_OF_LONG(UInt8Clamped)

#undef INVALID_INDEX

/* Compute a max size in chars required for not null-terminated string
 * representation of the specified integral or floating point type.
 */
template <typename T>
constexpr size_t MaxChars()
{
    static_assert(std::numeric_limits<T>::radix == 2U);
    if constexpr (std::is_integral_v<T>) {
        auto bits = std::numeric_limits<T>::digits;
        auto digitPerBit = std::log10(std::numeric_limits<T>::radix);
        auto digits = std::ceil(bits * digitPerBit) + static_cast<int>(std::is_signed_v<T>);
        return digits;
    } else {
        static_assert(std::is_floating_point_v<T>);
        // Forced to treat T as double
        auto bits = std::numeric_limits<double>::digits;
        auto digitPerBit = std::log10(std::numeric_limits<double>::radix);
        auto digits = std::ceil(bits * digitPerBit) + static_cast<int>(std::is_signed_v<double>);
        // digits + point + "+e308"
        return digits + 1U + std::char_traits<char>::length("+e308");
    }
}

template <typename T>
size_t NumberToU8Chars(PandaVector<char> &buf, size_t pos, T number)
{
    if constexpr (std::is_integral_v<T>) {
        auto [strEnd, result] = std::to_chars(&buf[pos], &buf[pos + MaxChars<T>()], number);
        ASSERT(result == std::errc());
        return strEnd - &buf[pos];
    } else {
        static_assert(std::is_floating_point_v<T>);
        // Forced to treat T as double
        auto asDouble = static_cast<double>(number);
        return intrinsics::helpers::FpToStringDecimalRadix(asDouble, [&buf, &pos](std::string_view str) {
            ASSERT(str.length() <= MaxChars<T>());
            auto err = memcpy_s(&buf[pos], MaxChars<T>(), str.data(), str.length());
            if (err != EOK) {
                UNREACHABLE();
            }
            return str.length();
        });
    }
}

template <typename T>
size_t NumberToU16Chars(PandaVector<EtsChar> &buf, size_t pos, T number)
{
    if constexpr (std::is_integral_v<T>) {
        auto num = number < 0 ? -static_cast<uint64_t>(number) : static_cast<uint64_t>(number);
        auto nDigits = (CountDigits(num) + static_cast<uint32_t>(number < 0));
        ASSERT(pos + nDigits <= buf.size());
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        utf::UInt64ToUtf16Array(num, buf.data() + pos, nDigits, number < 0);
        return nDigits;
    } else {
        static_assert(std::is_floating_point_v<T>);
        static_assert(std::is_unsigned_v<EtsChar>);
        // Forced to treat T as double
        auto asDouble = static_cast<double>(number);
        return intrinsics::helpers::FpToStringDecimalRadix(asDouble, [&buf, &pos](std::string_view str) {
            ASSERT(str.length() <= MaxChars<T>());
            for (size_t i = 0; i < str.length(); i++) {
                buf[pos + i] = static_cast<EtsChar>(str[i]);
            }
            return str.length();
        });
    }
}

template <typename T>
static ark::ets::EtsString *TypedArrayJoinUtf16(Span<T> &data, ark::ets::EtsString *separator)
{
    ASSERT(!data.empty());
    ASSERT(separator->IsUtf16());

    const size_t sepSize = separator->GetUtf16Length();
    PandaVector<EtsChar> buf(data.Size() * (MaxChars<T>() + sepSize));
    size_t strSize = 0;
    auto n = data.Size() - 1;
    if (sepSize == 1) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        auto sep = separator->GetDataUtf16()[0];
        for (auto i = 0U; i < n; i++) {
            strSize += NumberToU16Chars(buf, strSize, static_cast<T>(data[i]));
            buf[strSize] = sep;
            strSize += 1;
        }
    } else {
        for (auto i = 0U; i < n; i++) {
            strSize += NumberToU16Chars(buf, strSize, static_cast<T>(data[i]));
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            separator->CopyDataUtf16(buf.data() + strSize, sepSize);
            strSize += sepSize;
        }
    }
    strSize += NumberToU16Chars(buf, strSize, static_cast<T>(data[n]));
    return EtsString::CreateFromUtf16(reinterpret_cast<EtsChar *>(buf.data()), strSize);
}

template <typename T>
static ark::ets::EtsString *TypedArrayJoinUtf8(Span<T> &data)
{
    ASSERT(!data.empty());
    PandaVector<char> buf(data.Size() * MaxChars<T>());
    size_t strSize = 0;
    auto n = data.Size();
    for (auto i = 0U; i < n; i++) {
        strSize += NumberToU8Chars(buf, strSize, data[i]);
    }
    return EtsString::CreateFromUtf8(buf.data(), strSize);
}

template <typename T>
static ark::ets::EtsString *TypedArrayJoinUtf8(Span<T> &data, ark::ets::EtsString *separator)
{
    ASSERT(!data.empty());
    ASSERT(!separator->IsUtf16() && !separator->IsEmpty());
    const size_t sepSize = separator->GetUtf8Length();
    PandaVector<char> buf(data.Size() * (MaxChars<T>() + sepSize));
    size_t strSize = 0;
    auto n = data.Size() - 1;
    if (sepSize == 1) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        auto sep = separator->GetDataUtf8()[0];
        for (auto i = 0U; i < n; i++) {
            strSize += NumberToU8Chars(buf, strSize, data[i]);
            buf[strSize] = sep;
            strSize += 1;
        }
    } else {
        for (auto i = 0U; i < n; i++) {
            strSize += NumberToU8Chars(buf, strSize, data[i]);
            separator->CopyDataMUtf8(&buf[strSize], sepSize, false);
            strSize += sepSize;
        }
    }
    strSize += NumberToU8Chars(buf, strSize, data[n]);
    return EtsString::CreateFromUtf8(buf.data(), strSize);
}

template <typename T>
static ark::ets::EtsString *TypedArrayJoin(T *thisArray, ark::ets::EtsString *separator)
{
    using ElementType = typename T::ElementType;

    auto length = thisArray->GetLengthInt();
    if (UNLIKELY(length <= 0)) {
        return EtsString::CreateNewEmptyString();
    }

    void *dataPtr = GetNativeData(thisArray);
    if (UNLIKELY(dataPtr == nullptr)) {
        return nullptr;
    }

    /**
     * False-positive static-analyzer report:
     * GC can happen only on ThrowException in GetNativeData.
     * But such case meaning data to be nullptr and retun prevents
     * us from proceeding
     */
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto *tyDataPtr = reinterpret_cast<ElementType *>(static_cast<EtsByte *>(dataPtr) +
                                                      // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
                                                      static_cast<size_t>(thisArray->GetByteOffset()));
    Span<ElementType> data(tyDataPtr, length);
    // See (GetNativeData) reason
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    if (separator->IsEmpty()) {
        return TypedArrayJoinUtf8(data);
    }
    // See (GetNativeData) reason
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    if (!separator->IsUtf16()) {
        // See (GetNativeData) reason
        // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
        return TypedArrayJoinUtf8(data, separator);
    }
    // See (GetNativeData) reason
    // SUPPRESS_CSA_NEXTLINE(alpha.core.WasteObjHeader)
    return TypedArrayJoinUtf16(data, separator);
}

extern "C" ark::ets::EtsString *EtsEscompatInt8ArrayJoin(ark::ets::EtsEscompatInt8Array *thisArray,
                                                         ark::ets::EtsString *separator)
{
    return TypedArrayJoin(thisArray, separator);
}

extern "C" ark::ets::EtsString *EtsEscompatUint8ArrayJoin(ark::ets::EtsEscompatUInt8Array *thisArray,
                                                          ark::ets::EtsString *separator)
{
    return TypedArrayJoin(thisArray, separator);
}

extern "C" ark::ets::EtsString *EtsEscompatUint8ClampedArrayJoin(ark::ets::EtsEscompatUInt8ClampedArray *thisArray,
                                                                 ark::ets::EtsString *separator)
{
    return TypedArrayJoin(thisArray, separator);
}

extern "C" ark::ets::EtsString *EtsEscompatInt16ArrayJoin(ark::ets::EtsEscompatInt16Array *thisArray,
                                                          ark::ets::EtsString *separator)
{
    return TypedArrayJoin(thisArray, separator);
}

extern "C" ark::ets::EtsString *EtsEscompatUint16ArrayJoin(ark::ets::EtsEscompatUInt16Array *thisArray,
                                                           ark::ets::EtsString *separator)
{
    return TypedArrayJoin(thisArray, separator);
}

extern "C" ark::ets::EtsString *EtsEscompatInt32ArrayJoin(ark::ets::EtsEscompatInt32Array *thisArray,
                                                          ark::ets::EtsString *separator)
{
    return TypedArrayJoin(thisArray, separator);
}

extern "C" ark::ets::EtsString *EtsEscompatUint32ArrayJoin(ark::ets::EtsEscompatUInt32Array *thisArray,
                                                           ark::ets::EtsString *separator)
{
    return TypedArrayJoin(thisArray, separator);
}

extern "C" ark::ets::EtsString *EtsEscompatBigInt64ArrayJoin(ark::ets::EtsEscompatBigInt64Array *thisArray,
                                                             ark::ets::EtsString *separator)
{
    return TypedArrayJoin(thisArray, separator);
}

extern "C" ark::ets::EtsString *EtsEscompatBigUint64ArrayJoin(ark::ets::EtsEscompatBigUInt64Array *thisArray,
                                                              ark::ets::EtsString *separator)
{
    return TypedArrayJoin(thisArray, separator);
}

extern "C" ark::ets::EtsString *EtsEscompatFloat32ArrayJoin(ark::ets::EtsEscompatFloat32Array *thisArray,
                                                            ark::ets::EtsString *separator)
{
    return TypedArrayJoin(thisArray, separator);
}

extern "C" ark::ets::EtsString *EtsEscompatFloat64ArrayJoin(ark::ets::EtsEscompatFloat64Array *thisArray,
                                                            ark::ets::EtsString *separator)
{
    return TypedArrayJoin(thisArray, separator);
}

extern "C" EtsInt TypedArrayDoubleToInt(EtsDouble val, uint32_t bits = INT32_BITS)
{
    int32_t ret = 0;
    auto u64 = bit_cast<uint64_t>(val);
    int exp = static_cast<int>((u64 & DOUBLE_EXPONENT_MASK) >> DOUBLE_SIGNIFICAND_SIZE) - DOUBLE_EXPONENT_BIAS;
    if (exp < static_cast<int>(bits - 1)) {
        // smaller than INT<bits>_MAX, fast conversion
        ret = static_cast<int32_t>(val);
    } else if (exp < static_cast<int>(bits + DOUBLE_SIGNIFICAND_SIZE)) {
        // Still has significand bits after mod 2^<bits>
        // Get low <bits> bits by shift left <64 - bits> and shift right <64 - bits>
        uint64_t value = (((u64 & DOUBLE_SIGNIFICAND_MASK) | DOUBLE_HIDDEN_BIT)
                          << (static_cast<uint32_t>(exp) - DOUBLE_SIGNIFICAND_SIZE + INT64_BITS - bits)) >>
                         (INT64_BITS - bits);
        ret = static_cast<int32_t>(value);
        if ((u64 & DOUBLE_SIGN_MASK) == DOUBLE_SIGN_MASK && ret != INT32_MIN) {
            ret = -ret;
        }
    } else {
        // No significand bits after mod 2^<bits>, contains NaN and INF
        ret = 0;
    }
    return ret;
}

extern "C" EtsInt TypedArrayDoubleToInt32(EtsDouble val)
{
    return TypedArrayDoubleToInt(val, INT32_BITS);
}

extern "C" EtsShort TypedArrayDoubleToInt16(EtsDouble val)
{
    return TypedArrayDoubleToInt(val, INT16_BITS);
}

extern "C" EtsByte TypedArrayDoubleToInt8(EtsDouble val)
{
    return TypedArrayDoubleToInt(val, INT8_BITS);
}

extern "C" EtsLong TypedArrayDoubleToUint32(EtsDouble val)
{
    // Convert to int32_t first, then cast to uint32_t
    auto ret = TypedArrayDoubleToInt(val, INT32_BITS);
    auto pTmp = reinterpret_cast<uint32_t *>(&ret);
    return static_cast<EtsLong>(static_cast<uint32_t>(*pTmp));
}

extern "C" EtsInt TypedArrayDoubleToUint16(EtsDouble val)
{
    // Convert to int16_t first, then cast to uint16_t
    auto ret = TypedArrayDoubleToInt(val, INT16_BITS);
    auto pTmp = reinterpret_cast<int16_t *>(&ret);
    return static_cast<EtsInt>(static_cast<uint16_t>(*pTmp));
}

extern "C" EtsInt TypedArrayDoubleToUint8(EtsDouble val)
{
    // Convert to int8_t first, then cast to uint8_t
    auto ret = TypedArrayDoubleToInt(val, 8);
    auto pTmp = reinterpret_cast<uint8_t *>(&ret);
    return static_cast<EtsInt>(reinterpret_cast<uint8_t>(*pTmp));
}

template <typename T>
static T CastNumberHelper(double val) = delete;

template <typename T>
static T CastNumber(double val)
{
    if (std::isinf(val)) {
        return 0;
    }
    if (std::isnan(val)) {
        return 0;
    }
    return CastNumberHelper<T>(val);
}

template <>
EtsByte CastNumberHelper(double val)
{
    return TypedArrayDoubleToInt8(val);
}

template <>
EtsShort CastNumberHelper(double val)
{
    return TypedArrayDoubleToInt16(val);
}

template <>
EtsInt CastNumberHelper(double val)
{
    return TypedArrayDoubleToInt32(val);
}

template <>
EtsLong CastNumberHelper(double val)
{
    return StdCoreDoubleToLong(val);
}

template <typename T>
static T CastNumberUnsignedHelper(double val) = delete;

template <typename T>
static T CastNumberUnsigned(double val)
{
    if (std::isinf(val)) {
        return 0;
    }
    return CastNumberUnsignedHelper<T>(val);
}

template <>
uint8_t CastNumberUnsignedHelper(double val)
{
    return StdCoreDoubleToInt(val);
}

template <>
uint16_t CastNumberUnsignedHelper(double val)
{
    return StdCoreDoubleToInt(val);
}

template <>
uint32_t CastNumberUnsignedHelper(double val)
{
    return StdCoreDoubleToLong(val);
}

template <typename T1, typename T2>
static void EtsEscompatArrayOfImpl(T1 *thisArray, T2 *src)
{
    auto *arrayPtr = GetNativeData(thisArray);
    if (UNLIKELY(arrayPtr == nullptr)) {
        return;
    }

    using ElementType = typename T1::ElementType;
    auto *dst = reinterpret_cast<ElementType *>(ToUintPtr(arrayPtr) + static_cast<int>(thisArray->GetByteOffset()));
    std::copy_n(src, thisArray->GetLengthInt(), dst);
}

template <typename T1, typename T2>
static void EtsEscompatArrayOfImplNumber(T1 *thisArray, T2 *src)
{
    auto *arrayPtr = GetNativeData(thisArray);
    if (UNLIKELY(arrayPtr == nullptr)) {
        return;
    }

    using ElementType = typename T1::ElementType;
    auto *dst = reinterpret_cast<ElementType *>(ToUintPtr(arrayPtr) + static_cast<int>(thisArray->GetByteOffset()));
    for (int i = 0; i < thisArray->GetLengthInt(); i++) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        dst[i] = CastNumber<ElementType>(src[i]);
    }
}

template <typename T1, typename T2>
static void EtsEscompatArrayOfImplNumberUnsigned(T1 *thisArray, T2 *src)
{
    auto *arrayPtr = GetNativeData(thisArray);
    if (UNLIKELY(arrayPtr == nullptr)) {
        return;
    }

    using ElementType = typename T1::ElementType;
    auto *dst = reinterpret_cast<ElementType *>(ToUintPtr(arrayPtr) + static_cast<int>(thisArray->GetByteOffset()));
    for (int i = 0; i < thisArray->GetLengthInt(); i++) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        dst[i] = CastNumberUnsigned<ElementType>(src[i]);
    }
}

static int Clamp(double val)
{
    const int minUint8 = 0;
    const int maxUint8 = 255;
    if (val > maxUint8) {
        val = maxUint8;
    } else if (val < minUint8) {
        val = minUint8;
    }
    return val;
}

template <typename T1, typename T2>
static void EtsEscompatArrayOfImplClamped(T1 *thisArray, T2 *src)
{
    auto *arrayPtr = GetNativeData(thisArray);
    if (UNLIKELY(arrayPtr == nullptr)) {
        return;
    }

    using ElementType = typename T1::ElementType;
    auto *dst = reinterpret_cast<ElementType *>(ToUintPtr(arrayPtr) + static_cast<int>(thisArray->GetByteOffset()));
    for (int i = 0; i < thisArray->GetLengthInt(); i++) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        dst[i] = Clamp(src[i]);
    }
}

extern "C" void EtsEscompatInt8ArrayOfInt(ark::ets::EtsEscompatInt8Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsInt *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatInt8ArrayOfNumber(ark::ets::EtsEscompatInt8Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsDouble *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImplNumber(thisArray, srcAddress);
}

extern "C" void EtsEscompatInt8ArrayOfByte(ark::ets::EtsEscompatInt8Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsByte *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatInt16ArrayOfInt(ark::ets::EtsEscompatInt16Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsInt *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatInt16ArrayOfNumber(ark::ets::EtsEscompatInt16Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsDouble *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImplNumber(thisArray, srcAddress);
}

extern "C" void EtsEscompatInt16ArrayOfShort(ark::ets::EtsEscompatInt16Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsShort *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatInt32ArrayOfInt(ark::ets::EtsEscompatInt32Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsInt *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatInt32ArrayOfNumber(ark::ets::EtsEscompatInt32Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsDouble *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImplNumber(thisArray, srcAddress);
}

extern "C" void EtsEscompatBigInt64ArrayOfInt(ark::ets::EtsEscompatBigInt64Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsInt *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

template <typename T, typename ToLong>
void EtsEscompatArrayOfBigIntImpl(T *thisArray, EtsTypedObjectArray<EtsBigInt> *src, ToLong toLong)
{
    auto *arrayPtr = GetNativeData(thisArray);
    if (UNLIKELY(arrayPtr == nullptr)) {
        return;
    }

    using ElementType = typename T::ElementType;
    auto *dst = reinterpret_cast<ElementType *>(ToUintPtr(arrayPtr) + static_cast<int>(thisArray->GetByteOffset()));
    ASSERT(thisArray->GetLengthInt() >= 0 && static_cast<size_t>(thisArray->GetLengthInt()) >= src->GetLength());
    std::generate_n(dst, src->GetLength(), [idx = 0, src, toLong]() mutable { return toLong(src->Get(idx++)); });
}

extern "C" void EtsEscompatBigInt64ArrayOfBigInt(ark::ets::EtsEscompatBigInt64Array *thisArray, ark::ObjectHeader *src)
{
    // The method fills the typed array from a FixedArray<bigint> and in contrast to
    // EtsEscompatBigInt64ArraySetValuesFromArray, which fills a typed array from escompat.Array<bigint>, we can be
    // sure that `src` is an instance of the EtsTypedObjectArray<EtsBigInt> class.
    EtsEscompatArrayOfBigIntImpl(thisArray, EtsTypedObjectArray<EtsBigInt>::FromCoreType(src),
                                 [](EtsBigInt *bigint) { return unbox::GetLong(bigint); });
}

extern "C" void EtsEscompatBigInt64ArrayOfLong(ark::ets::EtsEscompatBigInt64Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsLong *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatBigInt64ArrayOfNumber(ark::ets::EtsEscompatBigInt64Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsDouble *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImplNumber(thisArray, srcAddress);
}

extern "C" void EtsEscompatFloat32ArrayOfInt(ark::ets::EtsEscompatFloat32Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsInt *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatFloat32ArrayOfFloat(ark::ets::EtsEscompatFloat32Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsFloat *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatFloat32ArrayOfNumber(ark::ets::EtsEscompatFloat32Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsDouble *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatFloat64ArrayOfInt(ark::ets::EtsEscompatFloat64Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsInt *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatFloat64ArrayOfNumber(ark::ets::EtsEscompatFloat64Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsDouble *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatUint8ClampedArrayOfNumber(ark::ets::EtsEscompatUInt8ClampedArray *thisArray,
                                                     EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsDouble *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImplClamped(thisArray, srcAddress);
}

extern "C" void EtsEscompatUint8ClampedArrayOfInt(ark::ets::EtsEscompatUInt8ClampedArray *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsInt *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImplClamped(thisArray, srcAddress);
}

extern "C" void EtsEscompatUint8ClampedArrayOfShort(ark::ets::EtsEscompatUInt8ClampedArray *thisArray,
                                                    EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsShort *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImplClamped(thisArray, srcAddress);
}

extern "C" void EtsEscompatUint8ArrayOfNumber(ark::ets::EtsEscompatUInt8Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsDouble *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImplNumberUnsigned(thisArray, srcAddress);
}

extern "C" void EtsEscompatUint8ArrayOfInt(ark::ets::EtsEscompatUInt8Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsInt *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatUint8ArrayOfShort(ark::ets::EtsEscompatUInt8Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsShort *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatUint16ArrayOfNumber(ark::ets::EtsEscompatUInt16Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsDouble *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImplNumberUnsigned(thisArray, srcAddress);
}

extern "C" void EtsEscompatUint16ArrayOfInt(ark::ets::EtsEscompatUInt16Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsInt *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatUint32ArrayOfNumber(ark::ets::EtsEscompatUInt32Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsDouble *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImplNumberUnsigned(thisArray, srcAddress);
}

extern "C" void EtsEscompatUint32ArrayOfInt(ark::ets::EtsEscompatUInt32Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsUint *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatUint32ArrayOfLong(ark::ets::EtsEscompatUInt32Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsLong *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatBigUint64ArrayOfInt(ark::ets::EtsEscompatBigUInt64Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsInt *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatBigUint64ArrayOfLong(ark::ets::EtsEscompatBigUInt64Array *thisArray, EtsCharArray *src)
{
    auto *srcAddress = reinterpret_cast<EtsLong *>(ToUintPtr(src->GetCoreType()->GetData()));
    EtsEscompatArrayOfImpl(thisArray, srcAddress);
}

extern "C" void EtsEscompatBigUint64ArrayOfBigInt(ark::ets::EtsEscompatBigUInt64Array *thisArray,
                                                  ark::ObjectHeader *src)
{
    // The method fills the typed array from a FixedArray<bigint> and in contrast to
    // EtsEscompatBigUint64ArraySetValuesFromArray, which fills a typed array from escompat.Array<bigint>, we can be
    // sure that `src` is an instance of the EtsTypedObjectArray<EtsBigInt> class.
    EtsEscompatArrayOfBigIntImpl(thisArray, EtsTypedObjectArray<EtsBigInt>::FromCoreType(src),
                                 [](EtsBigInt *bigint) { return unbox::GetULong(bigint); });
}
}  // namespace ark::ets::intrinsics
