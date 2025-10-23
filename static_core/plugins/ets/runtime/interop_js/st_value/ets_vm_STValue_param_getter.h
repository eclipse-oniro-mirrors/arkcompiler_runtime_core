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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_INTEROP_JS_ETS_VM_STVALUE_PARAM_GETTER_H
#define PANDA_PLUGINS_ETS_RUNTIME_INTEROP_JS_ETS_VM_STVALUE_PARAM_GETTER_H

#include <ani.h>
#include <js_native_api.h>
#include <js_native_api_types.h>
#include <node_api.h>
#include <variant>
#include "ets_coroutine.h"
#include "plugins/ets/runtime/interop_js/interop_context.h"

namespace ark::ets::interop::js {
bool GetString(napi_env env, napi_value jsVal, const std::string &paramName, std::string &paramValue);
bool GetArray(napi_env env, napi_value jsVal, const std::string &arrayName, std::vector<ani_value> &arrayValue);
bool GetReturnType(napi_env env, const std::string &signature, SType &type);
}  // namespace ark::ets::interop::js

#endif