/**
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#include "runtime/include/coretypes/array.h"

#include "runtime/arch/memory_helpers.h"
#include "runtime/include/class-inl.h"
#include "runtime/include/class_linker.h"
#include "runtime/include/coretypes/dyn_objects.h"
#include "runtime/include/runtime.h"
#include "runtime/include/panda_vm.h"

namespace panda::coretypes {

static Array *AllocateArray(panda::BaseClass *arrayClass, size_t elemSize, ArraySizeT length,
                            panda::SpaceType spaceType, const PandaVM *vm = Thread::GetCurrent()->GetVM())
{
    size_t size = Array::ComputeSize(elemSize, length);
    if (UNLIKELY(size == 0)) {
        LOG(ERROR, RUNTIME) << "Illegal array size: element size: " << elemSize << " array length: " << length;
        ThrowOutOfMemoryError("OOM when allocating array");
        return nullptr;
    }
    if (LIKELY(spaceType == panda::SpaceType::SPACE_TYPE_OBJECT)) {
        return static_cast<coretypes::Array *>(
            vm->GetHeapManager()->AllocateObject(arrayClass, size, DEFAULT_ALIGNMENT, ManagedThread::GetCurrent()));
    }
    if (spaceType == panda::SpaceType::SPACE_TYPE_NON_MOVABLE_OBJECT) {
        return static_cast<coretypes::Array *>(vm->GetHeapManager()->AllocateNonMovableObject(
            arrayClass, size, DEFAULT_ALIGNMENT, ManagedThread::GetCurrent()));
    }
    UNREACHABLE();
}

/* static */
Array *Array::Create(panda::Class *arrayClass, const uint8_t *data, ArraySizeT length, panda::SpaceType spaceType)
{
    size_t elemSize = arrayClass->GetComponentSize();
    auto *array = AllocateArray(arrayClass, elemSize, length, spaceType);
    if (UNLIKELY(array == nullptr)) {
        LOG(ERROR, RUNTIME) << "Failed to allocate array.";
        return nullptr;
    }
    // Order is matters here: GC can read data before it copied if we set length first.
    // length == 0 is guaranteed by AllocateArray
    TSAN_ANNOTATE_IGNORE_WRITES_BEGIN();
    array->SetLength(length);
    memcpy_s(array->GetData(), array->GetLength() * elemSize, data, length * elemSize);
    TSAN_ANNOTATE_IGNORE_WRITES_END();
    // Witout full memory barrier it is possible that architectures with weak memory order can try fetching array
    // legth before it's set
    arch::FullMemoryBarrier();
    return array;
}

/* static */
Array *Array::Create(panda::Class *arrayClass, ArraySizeT length, panda::SpaceType spaceType)
{
    size_t elemSize = arrayClass->GetComponentSize();
    auto *array = AllocateArray(arrayClass, elemSize, length, spaceType);
    if (array == nullptr) {
        return nullptr;
    }
    // No need to memset - it is done in allocator
    TSAN_ANNOTATE_IGNORE_WRITES_BEGIN();
    array->SetLength(length);
    TSAN_ANNOTATE_IGNORE_WRITES_END();
    // Without full memory barrier it is possible that architectures with weak memory order can try fetching array
    // length before it's set
    arch::FullMemoryBarrier();
    return array;
}

/* static */
Array *Array::Create(DynClass *dynarrayclass, ArraySizeT length, panda::SpaceType spaceType)
{
    size_t elemSize = coretypes::TaggedValue::TaggedTypeSize();
    HClass *arrayClass = dynarrayclass->GetHClass();
    auto *array = AllocateArray(arrayClass, elemSize, length, spaceType);
    if (array == nullptr) {
        return nullptr;
    }
    // No need to memset - it is done in allocator
    TSAN_ANNOTATE_IGNORE_WRITES_BEGIN();
    array->SetLength(length);
    TSAN_ANNOTATE_IGNORE_WRITES_END();
    // Witout full memory barrier it is possible that architectures with weak memory order can try fetching array
    // legth before it's set
    arch::FullMemoryBarrier();
    return array;
}

/* static */
Array *Array::CreateTagged(const PandaVM *vm, panda::BaseClass *arrayClass, ArraySizeT length,
                           panda::SpaceType spaceType, TaggedValue initValue)
{
    size_t elemSize = coretypes::TaggedValue::TaggedTypeSize();
    auto *array = AllocateArray(arrayClass, elemSize, length, spaceType, vm);
    if (array == nullptr) {
        return nullptr;
    }
    // Order is matters here: GC can read data before it copied if we set length first.
    // length == 0 is guaranteed by AllocateArray
    for (ArraySizeT i = 0; i < length; i++) {
        array->Set<TaggedType, false, true>(i, initValue.GetRawData());
    }
    TSAN_ANNOTATE_IGNORE_WRITES_BEGIN();
    array->SetLength(length);
    TSAN_ANNOTATE_IGNORE_WRITES_END();
    // Witout full memory barrier it is possible that architectures with weak memory order can try fetching array
    // legth before it's set
    arch::FullMemoryBarrier();
    return array;
}

}  // namespace panda::coretypes
