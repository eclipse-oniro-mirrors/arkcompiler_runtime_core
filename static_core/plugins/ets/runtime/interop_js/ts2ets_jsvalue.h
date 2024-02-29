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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_TS2ETS_TS2ETS_JSVALUE_H
#define PANDA_PLUGINS_ETS_RUNTIME_TS2ETS_TS2ETS_JSVALUE_H

#include "plugins/ets/runtime/interop_js/ts2ets_copy.h"

// This entire namespace is used for compatibility, use interop_context.h for new code instead
namespace panda::ets::ts2ets {

inline void InitJSValueExports()
{
    // do nothing
}

inline napi_value InvokeEtsMethodImpl(napi_env env, napi_value *jsargv, uint32_t jsargc, bool doClscheck)
{
    return interop::js::InvokeEtsMethodImpl(env, jsargv, jsargc, doClscheck);
}

namespace GlobalCtx {  // NOLINT(readability-identifier-naming)

void Init();

}  // namespace GlobalCtx

}  // namespace panda::ets::ts2ets

#endif  // !PANDA_PLUGINS_ETS_RUNTIME_TS2ETS_TS2ETS_JSVALUE_H
