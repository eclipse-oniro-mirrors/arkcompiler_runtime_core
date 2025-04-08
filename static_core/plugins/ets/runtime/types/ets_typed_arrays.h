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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_TYPES_ETS_TYPED_ARRAYS_H
#define PANDA_PLUGINS_ETS_RUNTIME_TYPES_ETS_TYPED_ARRAYS_H

#include "plugins/ets/runtime/types/ets_object.h"

namespace ark::ets {
template <typename T>
class EtsEscompatTypedArray : public EtsObject {
public:
    using ElementType = T;

    EtsEscompatTypedArray() = delete;
    ~EtsEscompatTypedArray() = delete;

    NO_COPY_SEMANTIC(EtsEscompatTypedArray);
    NO_MOVE_SEMANTIC(EtsEscompatTypedArray);

    static constexpr size_t GetBufferOffset()
    {
        return MEMBER_OFFSET(EtsEscompatTypedArray, buffer_);
    }

    static constexpr size_t GetByteOffsetOffset()
    {
        return MEMBER_OFFSET(EtsEscompatTypedArray, byteOffset_);
    }

    static constexpr size_t GetByteLengthOffset()
    {
        return MEMBER_OFFSET(EtsEscompatTypedArray, byteLength_);
    }

    static constexpr size_t GetNameOffset()
    {
        return MEMBER_OFFSET(EtsEscompatTypedArray, name_);
    }

    static constexpr size_t GetLengthIntOffset()
    {
        return MEMBER_OFFSET(EtsEscompatTypedArray, lengthInt_);
    }

    static constexpr size_t GetArrayBufferBackedOffset()
    {
        return MEMBER_OFFSET(EtsEscompatTypedArray, arrayBufferBacked_);
    }

    ObjectPointer<EtsObject> GetBuffer()
    {
        return buffer_;
    }

    EtsDouble GetByteOffset()
    {
        return byteOffset_;
    }

    EtsDouble GetByteLength()
    {
        return byteLength_;
    }

    EtsDouble GetBytesPerElement()
    {
        return bytesPerElement_;
    }

    EtsInt GetLengthInt()
    {
        return lengthInt_;
    }

    bool IsArrayBufferBacked()
    {
        return arrayBufferBacked_ != 0;
    }

    ObjectPointer<EtsString> GetName()
    {
        return name_;
    }

private:
    ObjectPointer<EtsObject> buffer_;
    EtsDouble bytesPerElement_;
    EtsDouble byteOffset_;
    EtsDouble byteLength_;
    EtsInt lengthInt_;
    EtsBoolean arrayBufferBacked_;
    ObjectPointer<EtsString> name_;
};

class EtsEscompatInt8Array : public EtsEscompatTypedArray<EtsByte> {};
class EtsEscompatInt16Array : public EtsEscompatTypedArray<EtsShort> {};
class EtsEscompatInt32Array : public EtsEscompatTypedArray<EtsInt> {};
class EtsEscompatBigInt64Array : public EtsEscompatTypedArray<EtsLong> {};
class EtsEscompatFloat32Array : public EtsEscompatTypedArray<EtsFloat> {};
class EtsEscompatFloat64Array : public EtsEscompatTypedArray<EtsDouble> {};
}  // namespace ark::ets

#endif  // PANDA_PLUGINS_ETS_RUNTIME_TYPES_ETS_TYPED_ARRAYS_H
