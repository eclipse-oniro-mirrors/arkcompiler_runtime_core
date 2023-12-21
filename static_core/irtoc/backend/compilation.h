/*
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

#ifndef PANDA_IRTOC_COMPILATION_H
#define PANDA_IRTOC_COMPILATION_H

#include "function.h"

namespace panda::irtoc {
using compiler::Graph;

class Compilation {
public:
    using Result = Expected<int, const char *>;

    Compilation() = default;

    Result Run();

    Result MakeElf(std::string_view output);

    template <typename T, typename... Args>
    static int RegisterUnit(Args &&...args)
    {
        static_assert(std::is_base_of_v<Function, T>);
        // CODECHECK-NOLINTNEXTLINE(CPP_RULE_ID_SMARTPOINTER_INSTEADOF_ORIGINPOINTER)
        units_.push_back(new T(std::forward<Args>(args)...));
        return 0;
    }

    Arch GetArch() const
    {
        return arch_;
    }

private:
    void CheckUsedRegisters();
    Result Compile();

private:
    // NOLINTNEXTLINE(fuchsia-statically-constructed-objects)
    static inline std::vector<Function *> units_;
    static inline bool initialized_ = false;
    Arch arch_ {RUNTIME_ARCH};
    std::regex methods_regex_;
    std::unique_ptr<ArenaAllocator> allocator_;
    std::unique_ptr<ArenaAllocator> local_allocator_;
#ifdef PANDA_COMPILER_DEBUG_INFO
    bool has_debug_info_ {false};
#endif
#if USE_VIXL_ARM64 && !PANDA_MINIMAL_VIXL && PANDA_LLVMAOT
    UsedRegisters used_registers_;
#endif
};
}  // namespace panda::irtoc

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define COMPILE(name)                                     \
    class name : public Function {                        \
    public:                                               \
        using Function::Function;                         \
        void MakeGraphImpl() override;                    \
        const char *GetName() const override              \
        {                                                 \
            return #name;                                 \
        }                                                 \
                                                          \
    private:                                              \
        static int dummy;                                 \
    };                                                    \
    int name ::dummy = Compilation::RegisterUnit<name>(); \
    void name ::MakeGraphImpl()

#endif  // PANDA_IRTOC_COMPILATION_H
