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

#ifndef ABCKIT_WRAPPER_LOGGER_H
#define ABCKIT_WRAPPER_LOGGER_H

#include "libarkbase/utils/logger.h"

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LOG_D LOG(DEBUG, ABCKIT_WRAPPER)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LOG_I LOG(INFO, ABCKIT_WRAPPER)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LOG_W LOG(WARNING, ABCKIT_WRAPPER)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LOG_E LOG(ERROR, ABCKIT_WRAPPER)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define LOG_F LOG(FATAL, ABCKIT_WRAPPER)

#endif  // ABCKIT_WRAPPER_LOGGER_H
