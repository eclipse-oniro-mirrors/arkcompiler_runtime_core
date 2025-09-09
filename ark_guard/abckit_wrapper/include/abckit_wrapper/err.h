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

#ifndef ABCKIT_WRAPPER_ERR_H
#define ABCKIT_WRAPPER_ERR_H

/**
 * @brief AbckitWrapperErrorCode
 * Subsystem Code(3-digit number): indicating the error subsystem. 113: bytecode obfuscation
 * Error Type Code(2-digit number): indicating the error type. 1x: Each system defines its own specific error types,
 * starting from 10. 12: abckit_wrapper obfuscation failed.
 * Extension Code(3-digit number): indicating the scene subdivision of the error type code.
 */
enum AbckitWrapperErrorCode {
    ERR_SUCCESS = 0,
    ERR_INNER_ERROR = 11312001,
    ERR_DUMP_ERROR = 11312002,
};

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
inline bool ABCKIT_WRAPPER_ERROR(const AbckitWrapperErrorCode &rc)
{
    return rc != AbckitWrapperErrorCode::ERR_SUCCESS;
}

#endif  // ABCKIT_WRAPPER_ERR_H
