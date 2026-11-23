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

#ifndef PANDA_PAOC_VALIDATION_H
#define PANDA_PAOC_VALIDATION_H

#include "paoc_options.h"

#include <optional>

namespace ark::paoc {

// Returns an error when a profile-guided compilation policy is used without a usable profile.
// Extracted from PaocInitializer::ValidatePaocOptions so the check can be unit-tested in-process,
// without spawning the ark_aot binary (whose on-disk location differs between CMake and GN builds).
inline std::optional<Options::Error> ValidateProfileGuidedPolicy(const Options &opts)
{
    if (opts.GetPaocCompilationPolicy() == "profile-guided" &&
        (!opts.IsPaocUseProfile() || !opts.WasSetPaocUseProfilePath() || opts.GetPaocUseProfilePath().empty())) {
        return Options::Error("--paoc-compilation-policy=profile-guided requires --paoc-use-profile:path=<file>");
    }
    return std::nullopt;
}

}  // namespace ark::paoc

#endif  // PANDA_PAOC_VALIDATION_H
