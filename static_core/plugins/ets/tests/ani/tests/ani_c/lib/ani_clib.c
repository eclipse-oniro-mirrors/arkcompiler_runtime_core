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

#include "ani.h"

#ifdef __cplusplus
#error "This file must be compile by the C compiler"
#endif

// NOLINTNEXTLINE(readability-identifier-naming)
ani_status ANI_CLIB_FindClass(ani_env *env, const char *classDescriptor, ani_class *result)
{
    return (*env)->FindClass(env, classDescriptor, result);
}
