/**
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#include "plugins/ets/stdlib/native/etsstdlib.h"
#include "plugins/ets/stdlib/native/core/Intl.h"

namespace ark::ets::stdlib {

// EtsNapiOnLoad needs to implement issue #18135
ets_int EtsNapiOnLoad(EtsEnv *env)
{
    // Initializing components
    ets_int hasError = ETS_OK;
    hasError += InitCoreIntl(env);
    return hasError == ETS_OK ? ETS_NAPI_VERSION_1_0 : ETS_ERR;
}

}  // namespace ark::ets::stdlib
