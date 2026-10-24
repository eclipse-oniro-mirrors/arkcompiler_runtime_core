/**
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#ifndef COMPILER_OPTIMIZER_IR_RANGE_INTRINSICS_H
#define COMPILER_OPTIMIZER_IR_RANGE_INTRINSICS_H

#include "runtime_interface.h"

namespace panda::compiler {

inline bool IsRangeIntrinsic(RuntimeInterface::IntrinsicId intrinsic_id)
{
    switch (intrinsic_id) {
        case RuntimeInterface::IntrinsicId::CALLRANGE_IMM8_IMM8_V8:
        case RuntimeInterface::IntrinsicId::WIDE_CALLRANGE_PREF_IMM16_V8:
        case RuntimeInterface::IntrinsicId::CALLTHISRANGE_IMM8_IMM8_V8:
        case RuntimeInterface::IntrinsicId::WIDE_CALLTHISRANGE_PREF_IMM16_V8:
        case RuntimeInterface::IntrinsicId::NEWOBJRANGE_IMM8_IMM8_V8:
        case RuntimeInterface::IntrinsicId::NEWOBJRANGE_IMM16_IMM8_V8:
        case RuntimeInterface::IntrinsicId::WIDE_NEWOBJRANGE_PREF_IMM16_V8:
        case RuntimeInterface::IntrinsicId::SUPERCALLTHISRANGE_IMM8_IMM8_V8:
        case RuntimeInterface::IntrinsicId::SUPERCALLARROWRANGE_IMM8_IMM8_V8:
        case RuntimeInterface::IntrinsicId::WIDE_SUPERCALLTHISRANGE_PREF_IMM16_V8:
        case RuntimeInterface::IntrinsicId::WIDE_SUPERCALLARROWRANGE_PREF_IMM16_V8:
        case RuntimeInterface::IntrinsicId::CREATEOBJECTWITHEXCLUDEDKEYS_IMM8_V8_V8:
        case RuntimeInterface::IntrinsicId::WIDE_CREATEOBJECTWITHEXCLUDEDKEYS_PREF_IMM16_V8_V8:
        case RuntimeInterface::IntrinsicId::CALLTHISRANGEWITHNAME_IMM8_IMM8_ID16_V8:
        case RuntimeInterface::IntrinsicId::WIDE_CALLTHISRANGEWITHNAME_PREF_IMM16_ID16_V8:
            return true;
        default:
            return false;
    }
}

}  // namespace panda::compiler

#endif  // COMPILER_OPTIMIZER_IR_RANGE_INTRINSICS_H
