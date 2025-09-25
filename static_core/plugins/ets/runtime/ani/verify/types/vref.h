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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_ANI_VERIFY_TYPES_VREF_H
#define PANDA_PLUGINS_ETS_RUNTIME_ANI_VERIFY_TYPES_VREF_H

#include "libarkbase/macros.h"
#include "plugins/ets/runtime/ani/ani.h"

namespace ark::ets::ani::verify {

// NOTE: Remove this enum
enum class ANIRefType {
    REFERENCE,
    MODULE,
    NAMESPACE,
    OBJECT,
    FN_OBJECT,
    ENUM_ITEM,
    ERROR,
    ARRAYBUFFER,
    STRING,
    ARRAY,
    TYPE,
    CLASS,
    ENUM,
    FIXED_ARRAY_BOOLEAN,
    FIXED_ARRAY_CHAR,
    FIXED_ARRAY_BYTE,
    FIXED_ARRAY_SHORT,
    FIXED_ARRAY_INT,
    FIXED_ARRAY_LONG,
    FIXED_ARRAY_FLOAT,
    FIXED_ARRAY_DOUBLE,
};

class VRef {
public:
    explicit VRef(ani_ref ref, ANIRefType type = ANIRefType::REFERENCE) : ref_(ref), type_(type) {}
    ~VRef() = default;

    ani_ref GetRef()
    {
        return ref_;
    }

    ANIRefType GetRefType()
    {
        return type_;
    }

    DEFAULT_COPY_SEMANTIC(VRef);
    DEFAULT_MOVE_SEMANTIC(VRef);

private:
    ani_ref ref_;
    ANIRefType type_;
};

class VModule final : public VRef {
public:
    ani_module GetRef()
    {
        return static_cast<ani_module>(VRef::GetRef());
    }
};

class VObject : public VRef {
public:
    ani_object GetRef()
    {
        return static_cast<ani_object>(VRef::GetRef());
    }
};

class VClass final : public VObject {
public:
    ani_class GetRef()
    {
        return static_cast<ani_class>(VObject::GetRef());
    }
};

class VString final : public VObject {
public:
    ani_string GetRef()
    {
        return static_cast<ani_string>(VObject::GetRef());
    }
};

class VError final : public VObject {
public:
    ani_error GetRef()
    {
        return static_cast<ani_error>(VObject::GetRef());
    }
};

class VFixedArray : public VObject {
public:
    ani_fixedarray GetRef()
    {
        return static_cast<ani_fixedarray>(VObject::GetRef());
    }
};

class VFixedArrayBoolean final : public VFixedArray {
public:
    ani_fixedarray_boolean GetRef()
    {
        return static_cast<ani_fixedarray_boolean>(VFixedArray::GetRef());
    }
};

class VFixedArrayChar final : public VFixedArray {
public:
    ani_fixedarray_char GetRef()
    {
        return static_cast<ani_fixedarray_char>(VFixedArray::GetRef());
    }
};

class VFixedArrayByte final : public VFixedArray {
public:
    ani_fixedarray_byte GetRef()
    {
        return static_cast<ani_fixedarray_byte>(VFixedArray::GetRef());
    }
};

class VFixedArrayShort final : public VFixedArray {
public:
    ani_fixedarray_short GetRef()
    {
        return static_cast<ani_fixedarray_short>(VFixedArray::GetRef());
    }
};

class VFixedArrayInt final : public VFixedArray {
public:
    ani_fixedarray_int GetRef()
    {
        return static_cast<ani_fixedarray_int>(VFixedArray::GetRef());
    }
};

class VFixedArrayLong final : public VFixedArray {
public:
    ani_fixedarray_long GetRef()
    {
        return static_cast<ani_fixedarray_long>(VFixedArray::GetRef());
    }
};

class VFixedArrayFloat final : public VFixedArray {
public:
    ani_fixedarray_float GetRef()
    {
        return static_cast<ani_fixedarray_float>(VFixedArray::GetRef());
    }
};

class VFixedArrayDouble final : public VFixedArray {
public:
    ani_fixedarray_double GetRef()
    {
        return static_cast<ani_fixedarray_double>(VFixedArray::GetRef());
    }
};

}  // namespace ark::ets::ani::verify

#endif  // PANDA_PLUGINS_ETS_RUNTIME_ANI_VERIFY_TYPES_VREF_H
