/**
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#ifndef PANDA_PROFILING_INL_H
#define PANDA_PROFILING_INL_H

#include "profiling.h"
#include "runtime/profiling/generated/profiling_includes_disasm.h"
#include "runtime/include/profiling_gen.h"

namespace ark::profiling {

inline Expected<ProfileContainer, const char *> ReadProfile([[maybe_unused]] std::istream &stm,
                                                            [[maybe_unused]] ark::panda_file::SourceLang lang)
{
    // NOLINTNEXTLINE(hicpp-multiway-paths-covered)
    switch (lang) {
#include "runtime/profiling/generated/read_profile.h"
        default:
            break;
    }
    return Unexpected("ReadProfile: No plugin found for the given language");
}

inline void DestroyProfile([[maybe_unused]] ProfileContainer profile, [[maybe_unused]] ark::panda_file::SourceLang lang)
{
    // NOLINTNEXTLINE(hicpp-multiway-paths-covered)
    switch (lang) {
#include "runtime/profiling/generated/destroy_profile.h"
        default:
            break;
    }
}

inline ProfileType FindMethodInProfile([[maybe_unused]] ProfileContainer profile,
                                       [[maybe_unused]] ark::panda_file::SourceLang lang,
                                       [[maybe_unused]] const std::string &methodName)
{
    // NOLINTNEXTLINE(hicpp-multiway-paths-covered)
    switch (lang) {
#include "runtime/profiling/generated/find_method_in_profile.h"
        default:
            LOG(FATAL, COMMON) << "FindMethodInProfile: No plugin found for the given language";
    }
    return INVALID_PROFILE;
}

inline void DumpProfile([[maybe_unused]] ProfileType profile, [[maybe_unused]] ark::panda_file::SourceLang lang,
                        [[maybe_unused]] BytecodeInstruction *inst, [[maybe_unused]] std::ostream &stm)
{
    // NOLINTNEXTLINE(hicpp-multiway-paths-covered)
    switch (lang) {
#include "runtime/profiling/generated/dump_profile.h"
        default:
            break;
    }
}
}  // namespace ark::profiling

#endif  // PANDA_PROFILING_INL_H