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
#ifndef PANDA_PLUGINS_ETS_RUNTIME_ETS_CLASS_ROOT_H
#define PANDA_PLUGINS_ETS_RUNTIME_ETS_CLASS_ROOT_H

#include "libpandabase/utils/logger.h"
#include "libpandabase/utils/type_helpers.h"
#include "runtime/include/class_root.h"

namespace panda::ets {

enum class EtsClassRoot {
    VOID = helpers::ToUnderlying(ClassRoot::V),
    BOOLEAN = helpers::ToUnderlying(ClassRoot::U1),
    BYTE = helpers::ToUnderlying(ClassRoot::I8),
    CHAR = helpers::ToUnderlying(ClassRoot::U16),
    SHORT = helpers::ToUnderlying(ClassRoot::I16),
    INT = helpers::ToUnderlying(ClassRoot::I32),
    LONG = helpers::ToUnderlying(ClassRoot::I64),
    FLOAT = helpers::ToUnderlying(ClassRoot::F32),
    DOUBLE = helpers::ToUnderlying(ClassRoot::F64),

    BOOLEAN_ARRAY = helpers::ToUnderlying(ClassRoot::ARRAY_U1),
    BYTE_ARRAY = helpers::ToUnderlying(ClassRoot::ARRAY_I8),
    CHAR_ARRAY = helpers::ToUnderlying(ClassRoot::ARRAY_U16),
    SHORT_ARRAY = helpers::ToUnderlying(ClassRoot::ARRAY_I16),
    INT_ARRAY = helpers::ToUnderlying(ClassRoot::ARRAY_I32),
    LONG_ARRAY = helpers::ToUnderlying(ClassRoot::ARRAY_I64),
    FLOAT_ARRAY = helpers::ToUnderlying(ClassRoot::ARRAY_F32),
    DOUBLE_ARRAY = helpers::ToUnderlying(ClassRoot::ARRAY_F64),

    CLASS = helpers::ToUnderlying(ClassRoot::CLASS),
    OBJECT = helpers::ToUnderlying(ClassRoot::OBJECT),
    STRING = helpers::ToUnderlying(ClassRoot::STRING),
    STRING_ARRAY = helpers::ToUnderlying(ClassRoot::ARRAY_STRING),
};

inline ClassRoot ToCoreClassRoot(EtsClassRoot etsClassRoot)
{
    return static_cast<ClassRoot>(etsClassRoot);
}

inline EtsClassRoot ToEtsClassRoot(ClassRoot classRoot)
{
    switch (classRoot) {
        // Primitives types
        case ClassRoot::V:
            return EtsClassRoot::VOID;
        case ClassRoot::U1:
            return EtsClassRoot::BOOLEAN;
        case ClassRoot::I8:
            return EtsClassRoot::BYTE;
        case ClassRoot::U16:
            return EtsClassRoot::CHAR;
        case ClassRoot::I16:
            return EtsClassRoot::SHORT;
        case ClassRoot::I32:
            return EtsClassRoot::INT;
        case ClassRoot::I64:
            return EtsClassRoot::LONG;
        case ClassRoot::F32:
            return EtsClassRoot::FLOAT;
        case ClassRoot::F64:
            return EtsClassRoot::DOUBLE;

        // Primitive arrays types
        case ClassRoot::ARRAY_U1:
            return EtsClassRoot::BOOLEAN_ARRAY;
        case ClassRoot::ARRAY_U8:
            return EtsClassRoot::BYTE_ARRAY;
        case ClassRoot::ARRAY_U16:
            return EtsClassRoot::CHAR_ARRAY;
        case ClassRoot::ARRAY_I16:
            return EtsClassRoot::SHORT_ARRAY;
        case ClassRoot::ARRAY_I32:
            return EtsClassRoot::INT_ARRAY;
        case ClassRoot::ARRAY_I64:
            return EtsClassRoot::LONG_ARRAY;
        case ClassRoot::ARRAY_F32:
            return EtsClassRoot::FLOAT_ARRAY;
        case ClassRoot::ARRAY_F64:
            return EtsClassRoot::DOUBLE_ARRAY;

        // Object types
        case ClassRoot::CLASS:
            return EtsClassRoot::CLASS;
        case ClassRoot::OBJECT:
            return EtsClassRoot::OBJECT;

        // Other types
        case ClassRoot::STRING:
            return EtsClassRoot::STRING;
        case ClassRoot::ARRAY_CLASS:
            return EtsClassRoot::STRING_ARRAY;
        default:
            LOG(FATAL, ETS) << "Unsupporeted class_root: " << helpers::ToUnderlying(classRoot);
    }

    UNREACHABLE();
}

}  // namespace panda::ets

#endif  // PANDA_PLUGINS_ETS_RUNTIME_ETS_CLASS_ROOT_H
