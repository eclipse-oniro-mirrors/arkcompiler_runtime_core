/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PANDA_PLUGINS_ETS_REFLECTION_HELPERS_
#define PANDA_PLUGINS_ETS_REFLECTION_HELPERS_

#include "ets_coroutine.h"
#include "plugins/ets/runtime/ets_utils.h"
#include "ets_class_linker_extension.h"
#include "libarkbase/utils/utils.h"
#include "intrinsics.h"
#include "types/ets_field.h"
#include "types/ets_object.h"
#include "plugins/ets/runtime/types/ets_string.h"
#include "plugins/ets/runtime/ets_exceptions.h"

namespace ark::ets::intrinsics::helpers {

struct PrimitiveFieldInfo {
    EtsObject *thisObj = nullptr;
    EtsObject *arg = nullptr;
    EtsClass *argClass = nullptr;
    EtsType fieldType = EtsType::UNKNOWN;
    EtsField *field = nullptr;
};

bool CheckReceiverType(EtsCoroutine *coro, EtsObject *arg, EtsClass *paramClass, Value *argValue);
EtsObject *InvokeAndResolveReturnValue(EtsMethod *method, EtsCoroutine *coro, Value *args);
EtsMethod *ValidateAndResolveInstanceMethod(EtsCoroutine *coro, EtsObject *thisObj, EtsMethod *method);
bool CheckPrimitiveReciever(EtsCoroutine *coro, EtsObject *arg, EtsClass *argClass, EtsClass *paramClass,
                            Value *argValue);
bool ValidateInstanceField(EtsCoroutine *coro, EtsObject *thisObj, EtsField *field);

template <typename T>
bool CheckAndUnpackBoxedType(EtsClassLinkerExtension *linkExt, EtsObject *arg, EtsClass *paramClass, Value *argValue,
                             ClassRoot primitiveRoot)
{
    if (paramClass != EtsClass::FromRuntimeClass(linkExt->GetClassRoot(primitiveRoot))) {
        return false;
    }
    *argValue = Value(EtsBoxPrimitive<T>::Unbox(arg));
    return true;
}

inline void ReflectFieldSetReference(EtsCoroutine *coro, EtsObject *thisObj, EtsObject *arg, EtsField *field)
{
    EtsHandle argH(coro, arg);
    EtsHandle thisObjH(coro, thisObj);
    if (field->IsStatic()) {
        field->GetDeclaringClass()->SetStaticFieldObject(field, arg);
    } else if (ValidateInstanceField(coro, thisObj, field)) {
        thisObjH->SetFieldObject(field, argH.GetPtr());
    }
}

template <typename T>
bool ReflectFieldSetPrimitive(EtsCoroutine *coro, EtsObject *thisObj, T primVal, EtsField *field)
{
    EtsHandle thisObjH(coro, thisObj);
    if (field->IsStatic()) {
        field->GetDeclaringClass()->SetStaticFieldPrimitive<T>(field, primVal);
        return true;
    } else if (ValidateInstanceField(coro, thisObjH.GetPtr(), field)) {
        thisObjH->SetFieldPrimitive<T>(field, primVal);
        return true;
    }
    return false;
}

inline EtsObject *ReflectFieldGetReference(EtsObject *thisObj, EtsField *field)
{
    if (field->IsStatic()) {
        return field->GetDeclaringClass()->GetStaticFieldObject(field);
    }
    return thisObj->GetFieldObject(field);
}

template <typename T>
Value ReflectFieldGetPrimitive(EtsObject *thisObj, EtsField *field)
{
    if (field->IsStatic()) {
        return Value(field->GetDeclaringClass()->GetStaticFieldPrimitive<T>(field));
    }
    return Value(thisObj->GetFieldPrimitive<T>(field));
}

bool ResolveAndSetPrimitive(EtsCoroutine *coro, const PrimitiveFieldInfo &info);
Value GetPrimitiveValue(EtsCoroutine *coro, EtsObject *thisObj, EtsType fieldType, EtsField *field);

}  // namespace ark::ets::intrinsics::helpers

#endif  // PANDA_PLUGINS_ETS_REFLECTION_HELPERS_
