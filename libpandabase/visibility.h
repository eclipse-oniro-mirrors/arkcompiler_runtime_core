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

#ifndef LIBPANDABASE_VISIBILITY_H
#define LIBPANDABASE_VISIBILITY_H

#ifndef PANDA_TARGET_WINDOWS
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PANDA_PUBLIC_API __attribute__((visibility ("default")))
#else
#define PANDA_PUBLIC_API __declspec(dllexport)
#endif

#endif // LIBPANDABASE_VISIBILITY_H
