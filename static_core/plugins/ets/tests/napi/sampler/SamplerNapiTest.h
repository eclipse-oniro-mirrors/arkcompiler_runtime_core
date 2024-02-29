/**
 * Copyright (c) 2021-2023 Huawei Device Co., Ltd.
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

#ifndef PANDA_PLUGINS_ETS_TESTS_NAPI_SAMPLER_SAMPLER_NAPI_TEST_H
#define PANDA_PLUGINS_ETS_TESTS_NAPI_SAMPLER_SAMPLER_NAPI_TEST_H

#include <ets_napi.h>

#ifdef __cplusplus
extern "C" {
#endif

//  NOLINTBEGIN(readability-identifier-naming, readability-named-parameter)

ETS_EXPORT ets_int ETS_CALL ETS_ETSGLOBAL_NativeSlowFunction(EtsEnv *, ets_class, ets_int);
ETS_EXPORT ets_int ETS_CALL ETS_ETSGLOBAL_NativeFastFunction(EtsEnv *, ets_class);
ETS_EXPORT ets_int ETS_CALL ETS_ETSGLOBAL_NativeNAPISlowFunction(EtsEnv *, ets_class, ets_int);
ETS_EXPORT ets_int ETS_CALL ETS_ETSGLOBAL_NativeNAPIFastFunction(EtsEnv *, ets_class, ets_int);

//  NOLINTEND(readability-identifier-naming, readability-named-parameter)

#ifdef __cplusplus
}
#endif

#endif  // PANDA_PLUGINS_ETS_TESTS_NAPI_SAMPLER_SAMPLER_NAPI_TEST_H
