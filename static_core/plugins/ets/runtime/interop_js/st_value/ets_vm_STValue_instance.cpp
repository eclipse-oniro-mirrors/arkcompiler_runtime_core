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

#include <ani.h>
#include <js_native_api.h>
#include <js_native_api_types.h>
#include <node_api.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include "ets_coroutine.h"
#include "ets_vm_STValue.h"
#include "include/mem/panda_containers.h"
#include "interop_js/interop_context.h"
#include "plugins/ets/runtime/ets_panda_file_items.h"
#include "plugins/ets/runtime/ets_vm_api.h"
#include "plugins/ets/runtime/interop_js/ets_proxy/ets_proxy.h"
#include "plugins/ets/runtime/interop_js/call/call.h"
#include "plugins/ets/runtime/interop_js/interop_common.h"
#include "plugins/ets/runtime/interop_js/code_scopes.h"

#include "generated/logger_options.h"
#include "compiler_options.h"
#include "compiler/compiler_logger.h"
#include "interop_js/napi_impl/napi_impl.h"
#include "plugins/ets/runtime/ets_utils.h"
#include "runtime/include/runtime.h"

#include "plugins/ets/runtime/interop_js/st_value/ets_vm_STValue.h"
#include "plugins/ets/runtime/interop_js/interop_context_api.h"

// NOLINTBEGIN(readability-identifier-naming, readability-redundant-declaration)
// CC-OFFNXT(G.FMT.10-CPP) project code style
napi_status __attribute__((weak)) napi_define_properties(napi_env env, napi_value object, size_t property_count,
                                                         const napi_property_descriptor *properties);
// NOLINTEND(readability-identifier-naming, readability-redundant-declaration)

namespace ark::ets::interop::js {

napi_value STValueClassInstantiateImpl(napi_env env, napi_callback_info info)
{
    ASSERT_SCOPED_NATIVE_CODE();
    NAPI_TO_ANI_SCOPE;
    auto *aniEnv = GetAniEnv();

    size_t jsArgc = 0;
    NAPI_CHECK_FATAL(napi_get_cb_info(env, info, &jsArgc, nullptr, nullptr, nullptr));
    if (jsArgc != 2U) {
        ThrowJSBadArgCountError(env, jsArgc, 2U);
        return nullptr;
    }

    napi_value jsThis;
    napi_value jsArgv[2];
    NAPI_CHECK_FATAL(napi_get_cb_info(env, info, &jsArgc, jsArgv, &jsThis, nullptr));

    std::vector<ani_value> argArray;
    if (!GetArrayFromNapiValue(env, jsArgv[1], argArray)) {
        return nullptr;
    }

    STValueData *clsData = reinterpret_cast<STValueData *>(GetSTValueDataPtr(env, jsThis));
    if (clsData == nullptr || !clsData->IsAniRef()) {
        ThrowJSThisNonObjectError(env);
        return nullptr;
    }
    ani_class clsClass = static_cast<ani_class>(clsData->GetAniRef());

    std::string ctorString = GetString(env, jsArgv[0]);

    ani_method ctorMethod {};
    AniCheckAndThrowToDynamic(env, aniEnv->Class_FindMethod(clsClass, "<ctor>", ctorString.c_str(), &ctorMethod));

    if (ctorMethod == nullptr) {
        STValueThrowJSError(env, "Instantiate ctor is not Null!;");
        return nullptr;
    }

    ani_object newObject {};
    if (!argArray.empty()) {
        AniCheckAndThrowToDynamic(env, aniEnv->Object_New_A(clsClass, ctorMethod, &newObject, argArray.data()));
    } else {
        AniCheckAndThrowToDynamic(env, aniEnv->Object_New(clsClass, ctorMethod, &newObject));
    }

    return CreateSTValueInstance(env, newObject);
}

// static newFixedArrayPrimitive(len: number, elementType: SType): STValue
napi_value STValueNewFixedArrayPrimitiveImpl(napi_env env, napi_callback_info info)
{
    ASSERT_SCOPED_NATIVE_CODE();
    NAPI_TO_ANI_SCOPE;
    auto *aniEnv = GetAniEnv();

    size_t jsArgc = 0;
    NAPI_CHECK_FATAL(napi_get_cb_info(env, info, &jsArgc, nullptr, nullptr, nullptr));

    if (jsArgc != 2U) {
        ThrowJSBadArgCountError(env, jsArgc, 2U);
        return nullptr;
    }

    napi_value jsArgv[2];
    NAPI_CHECK_FATAL(napi_get_cb_info(env, info, &jsArgc, jsArgv, nullptr, nullptr));

    uint32_t arrLength;
    NAPI_CHECK_FATAL(napi_get_value_uint32(env, jsArgv[0], &arrLength));

    SType arrayType = GetTypeFromType(env, jsArgv[1]);
    switch (arrayType) {
        case SType::BOOLEAN: {
            ani_fixedarray_boolean fixArray;
            AniCheckAndThrowToDynamic(env, aniEnv->FixedArray_New_Boolean(arrLength, &fixArray));
            return CreateSTValueInstance(env, fixArray);
        }
        case SType::CHAR: {
            ani_fixedarray_char fixArray {};
            AniCheckAndThrowToDynamic(env, aniEnv->FixedArray_New_Char(arrLength, &fixArray));
            return CreateSTValueInstance(env, fixArray);
        }
        case SType::BYTE: {
            ani_fixedarray_byte fixArray {};
            AniCheckAndThrowToDynamic(env, aniEnv->FixedArray_New_Byte(arrLength, &fixArray));
            return CreateSTValueInstance(env, fixArray);
        }
        case SType::SHORT: {
            ani_fixedarray_short fixArray {};
            AniCheckAndThrowToDynamic(env, aniEnv->FixedArray_New_Short(arrLength, &fixArray));
            return CreateSTValueInstance(env, fixArray);
        }
        case SType::INT: {
            ani_fixedarray_int fixArray {};
            AniCheckAndThrowToDynamic(env, aniEnv->FixedArray_New_Int(arrLength, &fixArray));
            return CreateSTValueInstance(env, fixArray);
        }
        case SType::LONG: {
            ani_fixedarray_long fixArray {};
            AniCheckAndThrowToDynamic(env, aniEnv->FixedArray_New_Long(arrLength, &fixArray));
            return CreateSTValueInstance(env, fixArray);
        }
        case SType::FLOAT: {
            ani_fixedarray_float fixArray {};
            AniCheckAndThrowToDynamic(env, aniEnv->FixedArray_New_Float(arrLength, &fixArray));
            return CreateSTValueInstance(env, fixArray);
        }
        case SType::DOUBLE: {
            ani_fixedarray_double fixArray {};
            AniCheckAndThrowToDynamic(env, aniEnv->FixedArray_New_Double(arrLength, &fixArray));
            return CreateSTValueInstance(env, fixArray);
        }
        default: {
            ThrowUnsupportedSTypeError(env, arrayType);
            return nullptr;
        }
    }
    return nullptr;
}

// static newFixedArrayReference(len: number, elementType: STValue, initialElement: STValue): STValue
napi_value STValueNewFixedArrayReferenceImpl(napi_env env, napi_callback_info info)
{
    ASSERT_SCOPED_NATIVE_CODE();
    NAPI_TO_ANI_SCOPE;
    auto *aniEnv = GetAniEnv();

    size_t jsArgc = 0;
    NAPI_CHECK_FATAL(napi_get_cb_info(env, info, &jsArgc, nullptr, nullptr, nullptr));
    if (jsArgc != 3U) {
        return nullptr;
    }

    napi_value jsArgv[3];
    NAPI_CHECK_FATAL(napi_get_cb_info(env, info, &jsArgc, jsArgv, nullptr, nullptr));

    uint32_t arrLength;
    NAPI_CHECK_FATAL(napi_get_value_uint32(env, jsArgv[0], &arrLength));

    STValueData *typeData = reinterpret_cast<STValueData *>(GetSTValueDataPtr(env, jsArgv[1]));
    if (typeData == nullptr || !typeData->IsAniRef()) {
        ThrowJSNonObjectError(env, "elementType");
        return nullptr;
    }
    STValueData *initData = reinterpret_cast<STValueData *>(GetSTValueDataPtr(env, jsArgv[2]));
    if (initData == nullptr || !initData->IsAniRef()) {
        ThrowJSNonObjectError(env, "initialElement");
        return nullptr;
    }

    ani_type arrayType = static_cast<ani_type>(typeData->GetAniRef());
    ani_ref initValue = static_cast<ani_ref>(initData->GetAniRef());

    ani_fixedarray_ref fixRefArray {};
    AniCheckAndThrowToDynamic(env, aniEnv->FixedArray_New_Ref(arrayType, arrLength, initValue, &fixRefArray));
    return CreateSTValueInstance(env, fixRefArray);
}
}  // namespace ark::ets::interop::js
